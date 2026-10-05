"""Revise the published DBText plot's legends without changing its measurements."""
from pathlib import Path
import csv
import hashlib
import json

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import NullLocator
from matplotlib.transforms import Bbox

OUT = Path(__file__).resolve().parent
ROOT = OUT
SNAPSHOT = OUT / "data"
PDF = OUT / "dbtext-random-access-comparison.pdf"
FRACTIONS = [1, 3, 10, 30, 100]
COLORS = ["#0072B2", "#D55E00", "#009E73", "#CC79A7"]
MARKERS = ["o", "v", "s", "D"]
ARMS = ["boundary-only", "from-scratch", "tools-allowed", "glm"]
TITLES = [
    "(a) Astra\nStandalone, without\nreference values",
    "(b) Astra\nStandalone, with\nreference values",
    "(c) Astra\nWith external\ndependencies",
    "(d) GLM\nWith external\ndependencies",
]
BASE_STYLES = [
    ("FSST", "FSST", "#777777", "--"),
    ("OnPair+ row adapter", "OnPair+", "#303030", "-."),
    ("LZ4 block", "LZ4", "#A4A4A4", ":"),
]


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


astra = read_csv(SNAPSHOT / "synthesized-code/dbtext/astra/data/row-points.csv")
glm = read_csv(SNAPSHOT / "synthesized-code/glm-5.3-flash/dbtext/tools-allowed/data/row-points.csv")
measurements = {}
input_paths = set()


def extract(rows, arm, stage=None, name=None):
    selected = sorted(
        (r for r in rows if r["arm"] == arm
         and (stage is None or int(r["stage"]) == stage)
         and (name is None or r["name"] == name)),
        key=lambda r: int(r["percent"]),
    )
    assert [int(r["percent"]) for r in selected] == FRACTIONS
    sizes = {int(r["package_bytes"]) for r in selected}
    assert len(sizes) == 1, "One stored package must serve all selection fractions"
    return {
        "package_bytes": sizes.pop(),
        "throughput_M_rows_s": [float(r["median"]) for r in selected],
        "source": selected[0]["source"],
        "result_id": selected[0]["result_id"],
    }


for arm, rows in [("from-scratch", astra), ("tools-allowed", astra), ("glm", glm)]:
    for stage in range(1, 5):
        measurements[arm, stage] = extract(rows, arm, stage=stage)
        if arm == "glm":
            provenance_path = SNAPSHOT / f"synthesized-code/glm-5.3-flash/dbtext/tools-allowed/G{stage}/provenance.json"
            provenance = json.loads(provenance_path.read_text())
            assert provenance["package_bytes"] == measurements[arm, stage]["package_bytes"]
            assert provenance["result_id"] == measurements[arm, stage]["result_id"]
            input_paths.add(provenance_path)

for stage in range(1, 5):
    path = SNAPSHOT / f"ablation/stage-{stage:02d}/QUALIFICATION.json"
    record = json.loads(path.read_text())
    measurements["boundary-only", stage] = {
        "package_bytes": record["package_bytes"],
        "throughput_M_rows_s": [record["rows_M_s"][str(p)]["median"] for p in FRACTIONS],
        "source": str(path.relative_to(ROOT)),
        "result_id": record["result_id"],
    }
    input_paths.add(path)

baselines = {name: extract(astra, "baseline", name=name) for name, *_ in BASE_STYLES}
for name in baselines:
    assert baselines[name] == extract(glm, "baseline", name=name)

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 7.2,
    "axes.titlesize": 7.6, "axes.labelsize": 8,
    "axes.spines.top": False, "axes.spines.right": False,
    "pdf.fonttype": 42, "ps.fonttype": 42,
})
fig, axes = plt.subplots(1, 4, figsize=(12.6 / 2.54, 3.15), sharex=True, sharey=True)
fig.subplots_adjust(left=.103, right=.985, top=.80, bottom=.45, wspace=.24)
legends = []
for panel, (ax, arm) in enumerate(zip(axes, ARMS)):
    for name, label, color, style in BASE_STYLES:
        ax.plot(FRACTIONS, baselines[name]["throughput_M_rows_s"],
                color=color, ls=style, lw=1.1, zorder=1)
    for stage in range(1, 5):
        ax.plot(FRACTIONS, measurements[arm, stage]["throughput_M_rows_s"],
                color=COLORS[stage - 1], marker=MARKERS[stage - 1],
                ms=3.3, lw=1.1, mfc="white", mew=.85, zorder=2)
    ax.set_title(TITLES[panel], pad=8, linespacing=1.18)
    ax.set_xscale("log")
    ax.set_xticks(FRACTIONS, [str(p) for p in FRACTIONS])
    ax.xaxis.set_minor_locator(NullLocator())
    ax.set_xlim(.75, 133)
    ax.set_ylim(0, 110)
    ax.set_yticks([0, 25, 50, 75, 100])
    ax.tick_params(length=2, pad=2, labelbottom=True)
    ax.grid(axis="y", color="#e6e6e6", lw=.5)
    ax.set_axisbelow(True)
    letter = "G" if arm == "glm" else "A"
    handles = [Line2D(
        [], [], color=COLORS[stage - 1], marker=MARKERS[stage - 1],
        ms=3.5, mfc="white", mew=.85, lw=1.1,
        label=f"{letter}{stage} ({measurements[arm, stage]['package_bytes'] / 1e6:.2f} MB)",
    ) for stage in range(1, 5)]
    center = (ax.get_position().x0 + ax.get_position().x1) / 2
    legends.append(fig.legend(
        handles=handles, loc="lower center", bbox_to_anchor=(center, .125),
        frameon=False, fontsize=7.0, handlelength=1.65, handletextpad=.55,
        labelspacing=.45, borderaxespad=0, borderpad=0,
    ))

fig.supylabel("Random-access throughput [M rows/s]", x=.004, y=.615, fontsize=8)
fig.supxlabel("Selected rows [%]", x=.544, y=.338, fontsize=8)
base_handles = [Line2D(
    [], [], color=color, ls=style, lw=1.1,
    label=f"{label} ({baselines[name]['package_bytes'] / 1e6:.2f} MB)",
) for name, label, color, style in BASE_STYLES]
legends.append(fig.legend(
    handles=base_handles, loc="lower center", bbox_to_anchor=(.544, .067),
    ncol=3, frameon=False, fontsize=7.1, handlelength=2.15,
    columnspacing=1.35, handletextpad=.55, borderpad=0, borderaxespad=0,
))
fig.text(.544, .014, "Original DBText: 39.84 MB",
         ha="center", va="bottom", fontsize=6.8, color="#444444")

# Verify the figure's labels, legends, and plot frames have separate space.
fig.canvas.draw()
renderer = fig.canvas.get_renderer()
texts = []
for ax in axes:
    texts.extend([ax.title] + ax.get_xticklabels() + ax.get_yticklabels())
texts.extend(fig.texts)
for legend in legends:
    texts.extend(legend.get_texts())
texts = [t for t in texts if t.get_visible() and t.get_text()]
boxes = [(text, text.get_window_extent(renderer)) for text in texts]
collisions = []
for index, (a, box_a) in enumerate(boxes):
    assert fig.bbox.contains(box_a.x0, box_a.y0) and fig.bbox.contains(box_a.x1, box_a.y1), a.get_text()
    for b, box_b in boxes[index + 1:]:
        intersection = Bbox.intersection(box_a, box_b)
        if intersection is not None and intersection.width > .3 and intersection.height > .3:
            collisions.append((a.get_text(), b.get_text()))
assert not collisions, collisions
for legend in legends:
    box = legend.get_window_extent(renderer)
    assert all(not box.overlaps(ax.get_window_extent(renderer)) for ax in axes)

PDF.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(PDF, facecolor="white", metadata={"Title": "DBText random access with compressed package sizes"})
fig.savefig(OUT / "dbtext-random-access-comparison.png", dpi=300, facecolor="white")
fig.savefig(OUT / "paper-size-preview.png", dpi=160, facecolor="white")
plt.close(fig)
input_paths.update([
    SNAPSHOT / "synthesized-code/dbtext/astra/data/row-points.csv",
    SNAPSHOT / "synthesized-code/glm-5.3-flash/dbtext/tools-allowed/data/row-points.csv",
])
audit = {
    "unit": "decimal MB = 1000000 bytes",
    "accounting": "Same package_bytes as the evaluated compression-factor data, including stored indexes. Standard library exemptions follow the existing benchmark.",
    "selection_percent": FRACTIONS,
    "runs": {f"{arm}/{stage}": record for (arm, stage), record in measurements.items()},
    "baselines": baselines,
    "inputs_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(input_paths)},
    "verification": {"text_collisions": collisions, "plotted_points": 95, "template_width_mm": 126},
}
(OUT / "size-and-curve-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
print(f"Verified 16 runs, 3 baselines, 95 unchanged measurements, and no overlapping labels.\n{PDF}")
