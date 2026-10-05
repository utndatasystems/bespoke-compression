"""Lay out the published CSV measurements at the LNI text width (126 mm)."""
import argparse
import csv
import hashlib
import json
import shutil
from math import atan2, degrees, hypot
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.colors import to_rgb
from matplotlib.ticker import FuncFormatter, NullLocator
from matplotlib.transforms import Bbox

OUT = Path(__file__).resolve().parent
DATA = OUT / "data"
COMMIT = "55b5592389ba53ecc9b26b8705fb600e1f4233aa"
COLORS = ["#0072B2", "#D55E00", "#009E73", "#CC79A7", "#6F51A3"]
GRAY, DARK = "#84909A", "#263B4D"
FRONT_LINEWIDTH = 1.0
BASE_COLORS = {"OnPair+":"#527D45", "Zstd":"#8B609A", "LZ4":"#AF5961",
               "Brotli":"#98702E", "XZ":"#397F89", "bzip2":"#93634C"}
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 7,
                    "axes.labelsize": 7, "axes.titlesize": 8,
                    "xtick.labelsize": 6.5, "ytick.labelsize": 6.5,
                    "axes.spines.top": False, "axes.spines.right": False,
                    "axes.linewidth": .6, "pdf.fonttype": 42,
                    "svg.fonttype": "none"})


def read(name, numeric):
    with (DATA / name).open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    for row in rows:
        for key in numeric:
            row[key] = float(row[key])
    return rows


def frontier(rows, x, y, dbtext=False):
    if dbtext:
        rows = [r for r in rows if r[x] > 1 and
                r["qualification"] != "accepted_pinned_result_with_portability_caveat"]
    return sorted([r for r in rows if not any(
        q[x] >= r[x] and q[y] >= r[y] and (q[x] > r[x] or q[y] > r[y])
        for q in rows)], key=lambda r: r[x])


def axes_style(ax):
    ax.grid(which="major", color="#E4E8EB", linewidth=.5)
    ax.set_axisbelow(True)
    ax.tick_params(length=2.5, width=.5, pad=2)
    ax.yaxis.set_minor_locator(NullLocator())
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{x:,.0f}"))


def baseline_color(name):
    return BASE_COLORS[name.split('-')[0].split()[0]]


def baseline_label(ax, label, point, position, color):
    return ax.annotate(label, point, xytext=position, textcoords="data", fontsize=5.8,
                color=color, ha="center", va="center", zorder=8,
                bbox=dict(boxstyle="round,pad=.22,rounding_size=.12",
                          fc=tuple(.92 + .08*c for c in to_rgb(color)), ec=color, lw=.65),
                arrowprops=dict(arrowstyle="-",color=color,lw=.5,shrinkA=2,shrinkB=3))


def outline(ax, rows, x, y, dbtext=False):
    for pool, style, color in [(rows[0], "--", GRAY), (rows[0] + rows[1], "-", DARK)]:
        pts = frontier(pool, x, y, dbtext)
        ax.plot([p[x] for p in pts], [p[y] for p in pts], style, color=color,
                linewidth=FRONT_LINEWIDTH, zorder=2)


def legend_line(label, color, marker=None, ls="-", linewidth=.9, **kw):
    return Line2D([], [], color=color, marker=marker, linestyle=ls,
                  markersize=3.8, markerfacecolor="white", linewidth=linewidth,
                  label=label, **kw)


def save(fig, name):
    # Fixed physical width prevents the document from shrinking the typography.
    fig.savefig(OUT / f"{name}.pdf", facecolor="white")
    fig.savefig(OUT / f"{name}.png", dpi=200, facecolor="white")
    plt.close(fig)


def label_collisions(ds, ax, labels, lines, renderer):
    boxes = [label.get_bbox_patch().get_window_extent(renderer).padded(.75)
             for label in labels]
    paths = [line.get_path().transformed(line.get_transform()) for line in lines]
    collisions = []
    for i, (label, box) in enumerate(zip(labels, boxes)):
        if any(path.intersects_bbox(box, filled=False) for path in paths):
            collisions.append(f"{ds}: {label.get_text()} crosses a frontier")
        if not all(ax.bbox.contains(*corner) for corner in box.get_points()):
            collisions.append(f"{ds}: {label.get_text()} extends outside axes")
        for other, other_box in zip(labels[:i], boxes[:i]):
            if box.overlaps(other_box):
                collisions.append(f"{ds}: {label.get_text()} overlaps {other.get_text()}")
    return collisions


def reposition_labels(fig, ax, labels, lines):
    """Move colliding text only; keep saved layouts and measured points intact."""
    paths = [line.get_path().transformed(line.get_transform()) for line in lines]
    points = [Bbox.from_bounds(x-3, y-3, 6, 6)
              for line in ax.lines[len(lines):]
              for x, y in line.get_transform().transform(line.get_xydata())]
    for label in labels:
        renderer = fig.canvas.get_renderer()
        box = label.get_bbox_patch().get_window_extent(renderer).padded(.75)
        others = [other.get_bbox_patch().get_window_extent(renderer).padded(.75)
                  for other in labels if other is not label]

        def clear(candidate):
            return (all(ax.bbox.contains(*p) for p in candidate.get_points())
                    and not any(path.intersects_bbox(candidate, filled=False) for path in paths)
                    and not any(candidate.overlaps(other) for other in others))

        if clear(box):
            continue
        cx, cy = (box.x0+box.x1)/2, (box.y0+box.y1)/2
        candidates = [(ax.bbox.x0+ax.bbox.width*x/40,
                       ax.bbox.y0+ax.bbox.height*y/30)
                      for x in range(1, 40) for y in range(1, 30)]
        candidates.sort(key=lambda p: (p[0]-cx)**2+(p[1]-cy)**2)
        for x, y in candidates:
            candidate = box.translated(x-cx, y-cy)
            if clear(candidate) and not any(candidate.overlaps(p) for p in points):
                transform = label.get_transform()
                px, py = transform.transform(label.get_position())
                label.set_position(transform.inverted().transform((px+x-cx, py+y-cy)))
                fig.canvas.draw()
                break


def bulk_workloads():
    """Four Pareto panels with zero-based linear axes and unchanged measurements.

    Compare package compression factor and bulk throughput for 4 Astra runs
    and the pre-Astra baseline frontier in each dataset. Preserve the existing
    run colors, hollow diamonds, colored baseline circles, and frontier styles.
    Use the same 126 mm PDF width, a two-row legend, and detached direction
    arrows; validate label clearance before export and inspect in Overleaf.
    """
    fig, axs = plt.subplots(2,2,figsize=(126/25.4,4.25))
    # Keep text sizes and horizontal geometry; shorten only the panel height.
    fig.subplots_adjust(left=.095, right=.995, top=1-.045*4.72/4.25,
                        bottom=(4.4-.78*4.72)/4.25, wspace=.16, hspace=.25)
    datasets = [("openstack","OpenStack",(0,33),[0,10,20,30],14,range(0,15,2)),
                ("python","Python",(0,6.3),[0,2,4,6],7,range(8)),
                ("yelp","Yelp",(0,16),[0,5,10,15],10,range(0,11,2)),
                ("sqlstorm-tpcds","SQLStorm TPC-DS",(0,22),[0,5,10,15,20],14,range(0,15,2))]
    # Current repository labels, repositioned to fit the grouped paper layout.
    baseline_labels = {
        "python": {"OnPair+": ("OnPair+", (1.35,3.4)),
                   "Zstd-20": ("Zstandard-19/20/22", (1.7,1.5)), "XZ-9": ("XZ-9", (3.2,.43))},
        "yelp": {"OnPair+": ("OnPair+", (6.7,6.4)),
                 "LZ4 HC-12": ("LZ4 HC-12", (6.7,2.2)),
                 "Zstd-22": ("Zstandard-19-22", (10.7,1.0))},
        "openstack": {"LZ4 HC-12": ("LZ4 HC-9/12", (14,8.5)),
                      "Zstd-22": ("Zstandard-19/22", (6,2.4)),
                      "Brotli-11 (default)": ("Brotli-11", (22.5,1.0))},
        "sqlstorm-tpcds": {"OnPair+": ("OnPair+", (3.2,8.7)),
                           "LZ4 HC-12": ("LZ4 HC-12", (3.5,4.1)),
                           "Zstd-21": ("Zstandard-19/21/22", (5.7,2.07)),
                           "bzip2-9 (default)": ("bzip2-9", (11.8,.70))},
    }
    run_offsets = {
        "python": {"1": (5,7), "2": (0,18), "3": (-8,7), "4": (2,14)},
        "yelp": {"1": (5,7), "2": (0,18), "3": (-7,10), "4": (5,10)},
        "openstack": {"1": (-5,3), "2": (0,18), "3": (5,8), "4": (5,8)},
        "sqlstorm-tpcds": {"1": (5,7), "2": (0,14), "3": (5,8), "4": (5,10)},
    }
    label_groups = []
    for idx, (ax, (ds,title,xlim,xticks,ymax,yticks)) in enumerate(zip(axs.flat,datasets)):
        base=read(f"{ds}-baseline.csv",["compression_factor","decompression_median_MB_s"])
        runs=read(f"{ds}-generated.csv",["compression_factor","decompression_median_MB_s"])
        x,y="compression_factor","decompression_median_MB_s"
        for p in base+runs: p[y]/=1000
        assert len(base)==22 and len(runs)==4
        base_frontier=frontier(base,x,y)
        outline(ax,(base_frontier,runs),x,y)
        frontier_lines = list(ax.lines)
        point_labels = []
        for p in base_frontier:
            color=baseline_color(p["method"])
            ax.plot(p[x],p[y],"o",color=color,ms=3,zorder=4,clip_on=False)
            if p["method"] in baseline_labels[ds]:
                label,position=baseline_labels[ds][p["method"]]
                point_labels.append(baseline_label(ax,label,(p[x],p[y]),position,color))
        for p in runs:
            color=COLORS[int(p["run"])-1]
            ax.plot(p[x],p[y],"D",color=color,mfc="white",mew=1,ms=4,zorder=5,clip_on=False)
            dx,dy=run_offsets[ds][p["run"]]
            point_labels.append(ax.annotate("A"+p["run"],(p[x],p[y]),xytext=(dx,dy),textcoords="offset points",
                        color=color,fontsize=6.5,ha="right" if dx<0 else ("center" if dx==0 else "left"),va="center",zorder=10,
                        bbox=dict(fc="white",ec="none",pad=.7)))
        ax.set(xlim=xlim,ylim=(0,ymax),yscale="linear")
        ax.set_xticks(xticks)
        ax.xaxis.set_major_formatter(FuncFormatter(lambda v,_:f"{v:g}×"))
        ax.set_yticks(yticks)
        if idx%2==0: ax.set_ylabel("Decompression [GB/s]")
        if idx>=2: ax.set_xlabel("Compression factor")
        ax.set_title(f"({chr(97+idx)}) {title}",loc="center",pad=5)
        ax.annotate("", xy=(.94,.92), xytext=(.77,.70), xycoords="axes fraction",
                    arrowprops=dict(arrowstyle="-|>", color=DARK, lw=.9, mutation_scale=8),
                    zorder=7)
        # Center the label along the arrow, with a 1.5 pt perpendicular gap.
        start, end = ax.transAxes.transform([(.77, .70), (.94, .92)])
        dx, dy = end - start
        length = hypot(dx, dy)
        ax.annotate("Better", xy=(.855, .81), xycoords="axes fraction",
                    xytext=(-1.5*dy/length, 1.5*dx/length), textcoords="offset points",
                    fontsize=6.5, color=DARK, rotation=degrees(atan2(dy, dx)),
                    rotation_mode="anchor", ha="center", va="bottom", zorder=7)
        axes_style(ax)
        label_groups.append((ds, ax, point_labels, frontier_lines))
    names=["A1 Max. decoding speed", "A2 Max. compression",
           "A3 Max. speed at compression target", "A4 Max. compression at speed target"]
    handles=[legend_line(n,COLORS[i],"D","none") for i,n in enumerate(names)]
    handles += [legend_line("Traditional front",GRAY,None,"--",linewidth=FRONT_LINEWIDTH),
                legend_line("Combined front",DARK,linewidth=FRONT_LINEWIDTH)]
    legend=fig.legend(handles=handles,loc="lower center",bbox_to_anchor=(.51,.003),
                      ncol=3,frameon=False,fontsize=6.2,handlelength=1.5,
                      columnspacing=.9,labelspacing=.65)
    fig.canvas.draw()
    renderer = fig.canvas.get_renderer()
    collisions = []
    if not all(fig.bbox.contains(*corner) for corner in legend.get_window_extent(renderer).get_points()):
        collisions.append("Legend extends outside figure")
    for ds, ax, labels, lines in label_groups:
        if label_collisions(ds, ax, labels, lines, renderer):
            reposition_labels(fig, ax, labels, lines)
            renderer = fig.canvas.get_renderer()
        collisions.extend(label_collisions(ds, ax, labels, lines, renderer))
    if collisions:
        plt.close(fig)
        raise ValueError("Label layout requires adjustment:\n" + "\n".join(collisions))
    save(fig,"bulk-datasets-grouped")


if __name__ == "__main__":
    bulk_workloads()
