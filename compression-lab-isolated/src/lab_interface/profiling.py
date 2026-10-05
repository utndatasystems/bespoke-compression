"""Separate perf-stat diagnostics. These are explicitly whole-process counters.

The retained official timer and its seven-trial results are never instrumented
or overwritten. Unsupported/denied hardware counters are not reported as zero.
"""
from contextlib import contextmanager
import csv
import ctypes
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import tempfile
import sys

from .api import SCORING, _clean_env
from . import PROJECT
from compression_lab import strings
from compression_lab.util import Error, load, lock, now, save, sha

EVENTS = ("cycles:u", "instructions:u", "cache-references:u", "cache-misses:u", "branches:u", "branch-misses:u")
SCOPE = ("Whole process tree on the selected CPU: namespace startup, dynamic loading, archive I/O, "
         "setup, cold and warm full reconstruction, output verification and cleanup. "
         "These are diagnostic counters, not decode-only counters or the official timing samples.")


def capabilities():
    try:
        paranoid = int(Path("/proc/sys/kernel/perf_event_paranoid").read_text())
    except (OSError, ValueError):
        paranoid = None
    return {"backend": "perf stat", "installed": bool(shutil.which("perf")),
            "perf_event_paranoid": paranoid, "events": list(EVENTS), "scope": SCOPE,
            "access": "verified by a real probe when a profile job runs"}


def parse_counters(path):
    rows = []
    for fields in csv.reader(Path(path).read_text().splitlines(), delimiter=";"):
        if len(fields) < 3 or not fields[0].strip() or fields[0].startswith("#"):
            continue
        try:
            value = float(fields[0].strip())
        except ValueError:
            value = None
        def number(index):
            try:
                return float(fields[index].strip().rstrip("%"))
            except (ValueError, IndexError):
                return None
        rows.append({"event": fields[2].strip(), "value": value,
                     "counter_status": "counted" if value is not None else fields[0].strip(),
                     "counter_runtime_ns": number(3), "time_running_percent": number(4)})
    return rows


@contextmanager
def replay_command(config, decoder, archive, output, capacity):
    """Minimal filesystem and the retained no-network/no-clone/affinity filter.

    perf runs outside the filter; the candidate cannot open performance events.
    """
    from compression_lab import _sandbox
    _, mounts = strings._closure([config["driver"]["path"], decoder], config)
    mounts.update({"/candidate/driver": config["driver"]["path"],
                   "/candidate/decoder.so": str(decoder), "/input/archive": str(archive)})
    output.mkdir()
    with tempfile.TemporaryFile() as bpf:
        if config.get('server_local'):
            # Same direct Landlock/seccomp entrypoint as native server evaluation;
            # perf remains outside the candidate filter.
            policy=output.parent/(output.name+'-sandbox.json')
            argv=[str(config['driver']['path']),str(decoder),'decode',str(archive),
                  str(output/'data'),str(capacity),'/dev/null']
            save(policy,dict(argv=argv,reads=[*mounts.values(),str(decoder),str(archive),'/dev/null'],
                output=str(output),cpu=config['cpu'],sanitizer=False,
                library_path=':'.join(sorted({str(Path(p).parent) for p in mounts.values()}))))
            yield [sys.executable,str(PROJECT/'remote-entry.py'),'sandbox',str(policy)],bpf.fileno()
            return
        library, context = _sandbox.filter_setup(False, 1, pin_affinity=True)
        library.seccomp_export_bpf.argtypes = [ctypes.c_void_p, ctypes.c_int]
        try:
            if library.seccomp_export_bpf(context, bpf.fileno()):
                raise Error("seccomp_export_failed")
        finally:
            library.seccomp_release(context)
        bpf.seek(0)
        args = ["/usr/bin/bwrap", "--unshare-all", "--die-with-parent", "--new-session",
                "--cap-drop", "ALL", "--clearenv"]
        for target, source in mounts.items():
            args += ["--ro-bind", source, target]
        args += ["--bind", str(output), "/output", "--tmpfs", "/tmp", "--dev", "/dev",
                 "--setenv", "LD_LIBRARY_PATH", "/lib", "--chdir", "/output",
                 "--seccomp", str(bpf.fileno()), "--", "/candidate/driver", "/candidate/decoder.so",
                 "decode", "/input/archive", "/output/data", str(capacity), "/dev/null"]
        yield args, bpf.fileno()


def _run(argv, folder, *, pass_fds=(), timeout=None):
    with (folder / "stdout.txt").open("wb") as out, (folder / "stderr.txt").open("wb") as err:
        # This helper runs only in a detached, single-threaded worker.
        def limits():
            resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
            resource.setrlimit(resource.RLIMIT_FSIZE, (128 * 1024**2, 128 * 1024**2))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        process = subprocess.Popen(argv, stdout=out, stderr=err, stdin=subprocess.DEVNULL, env=_clean_env(),
                                   start_new_session=True, pass_fds=pass_fds, preexec_fn=limits)
        try:
            return process.wait(timeout=timeout)
        finally:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()


def run(lab, job, state):
    source = lab.result(state["source_result_id"])
    source_job = lab.owner / "jobs" / source["job_id"]
    row = {"kind": "profile", "job_id": state["job_id"], "source_result_id": source["result_id"],
           "candidate": source["candidate"], "scoring": SCORING, "capabilities": capabilities(),
           "scope": SCOPE, "cpu": lab.config["cpu"], "status": "unavailable", "counters": None}
    output = job / "profile"
    output.mkdir()
    if lab.config.get('remote'):
        from .remote import diagnostics
        manifest=load(source_job/'submission.json')['manifest']
        returned=diagnostics(lab,job,kind='profile',driver=source_job/'harness/driver',
            decoder=source_job/'candidate'/manifest['decoder'],archives=source_job/'archives',
            libraries={p.name:p for p in (source_job/'libraries').iterdir()},columns=lab.config['columns'])
        shutil.copytree(returned,output/'remote')
        records=load(returned/'CALLS.json')
        row['cpu']=lab.config['remote']['cpu']
        if any(r['returncode'] or r['reason_code'] for r in records):
            row['reason']='remote_hardware_counters_or_replay_unavailable'
            row['diagnostic']=records[-1]['stderr'][-4000:]
        else:
            row.update(status='complete',reason=None,counters=[
                dict(column=col['name'],exact=True,counters=parse_counters(returned/'checks'/f'{i}-perf.csv'))
                for i,col in enumerate(lab.config['columns'])])
        return row
    perf = shutil.which("perf")
    if not perf:
        row["reason"] = "perf_not_installed"
        save(output / "capabilities.json", row["capabilities"])
        return row
    with lock(Path(lab.config["host_lock"]), host=True):
        with lock(job / "state.lock"):
            state.update(status="running", started_utc=now())
            save(job / "status.json", state)
        probe = output / "probe"
        probe.mkdir()
        def prefix(folder):
            return ["/usr/bin/taskset", "-c", str(lab.config["cpu"]), perf, "stat", "--no-big-num",
                    "-x", ";", "-o", str(folder / "counters.csv"), "-e", ",".join(EVENTS), "--"]
        code = _run([*prefix(probe), "/usr/bin/true"], probe, timeout=None)
        counters = parse_counters(probe / "counters.csv") if (probe / "counters.csv").exists() else []
        if code or not counters or all(c["value"] is None for c in counters):
            row["reason"] = "hardware_counters_unavailable"
            row["diagnostic"] = (probe / "stderr.txt").read_text()[:4000]
            return row
        config = strings.config(source_job / "engine")
        manifest = load(source_job / "submission.json")["manifest"]
        decoder = source_job / "candidate" / manifest["decoder"]
        records = []
        for column in lab.config["columns"]:
            folder = output / column["name"]
            folder.mkdir()
            archive = source_job / "archives" / (column["sha256"] + ".bin")
            with replay_command(config, decoder, archive, folder / "output", column["bytes"] + 32) as (argv, fd):
                code = _run([*prefix(folder), *argv], folder, pass_fds=(fd,))
            reconstructed = folder / "output/data"
            if code or not reconstructed.exists() or sha(reconstructed) != column["sha256"]:
                raise Error("profile_replay_failed")
            values = parse_counters(folder / "counters.csv")
            if not values or all(c["value"] is None for c in values):
                row["reason"] = "hardware_counters_unavailable_during_replay"
                return row
            records.append({"column": column["name"], "exact": True, "counters": values})
            reconstructed.unlink()  # disposable reconstruction; original input and archive retained
        row.update(status="complete", reason=None, counters=records)
    return row
