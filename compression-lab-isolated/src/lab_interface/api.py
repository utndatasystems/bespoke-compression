"""Owner provisioning, researcher isolation, immutable submissions and receipts.

This does not assign a paper-compatible score or certify a new benchmark protocol.
The strings-v1 engine supplies development measurements pending that decision.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import zipfile

from . import PROJECT, __version__
from compression_lab import candidate, runner, strings
from compression_lab.util import (Error, digest, ident, load, lock, now, rel, save,
                                  secure_load, sha)


SCORING = {"status": "pending_owner_decision", "primary_score": None}
ACTIVE = {"queued", "running", "cancelling"}


def interface_pins():
    paths=[*(PROJECT/'src/lab_interface').glob('*.py'),*(PROJECT/'include').glob('*'),PROJECT/'remote-entry.py',PROJECT/'server-shell-entry.py']
    return {str(p.relative_to(PROJECT)):sha(p) for p in paths if p.is_file()}


def verify_runtime():
    receipt = load(PROJECT / "runtime-lock.json")
    for name, expected in receipt["files"].items():
        path = PROJECT / "vendor" / rel(name)
        if path.stat().st_size != expected["bytes"] or sha(path) != expected["sha256"]:
            raise Error("pinned_runtime_changed", name)
    return {"version": receipt["version"], "manifest_sha256": sha(PROJECT / "runtime-lock.json")}


def _clean_env():
    return {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8",
            "PYTHONPATH": str(PROJECT / "src"), "PYTHONDONTWRITEBYTECODE": "1"}


def _identity(pid):
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return fields[19] if fields[0] not in ("Z", "X") else None
    except (OSError, IndexError):
        return None


def initialize(root, inputs, *, cpu, row_framing="lf", host_lock=None, server_local=False):
    """Create a NEW workspace. This compiles only the trusted measurement driver."""
    runtime = verify_runtime()
    root = Path(root).absolute()
    if root.exists():
        raise Error("workspace_already_exists")
    if cpu not in os.sched_getaffinity(0):
        raise Error("cpu_unavailable")
    if row_framing not in strings.ROW_FRAMINGS:
        raise Error("invalid_row_framing")
    paths = [Path(p).resolve(strict=True) for p in inputs]
    if not paths or len({p.name for p in paths}) != len(paths):
        raise Error("invalid_inputs")
    # Native measurements use the engine's strict runner, never an unsafe fallback.
    if server_local:
        import ctypes
        from .server_shell import proot_path
        abi=ctypes.CDLL(None).syscall(444,0,0,1)
        probe={'status':'available' if abi>=3 and proot_path().is_file() else 'unavailable'}
    else:
        probe = runner.probe()
    if probe["status"] != "available":
        raise Error("measurement_sandbox_unavailable")
    root.mkdir(parents=True, mode=0o700)
    for directory in ("public/inputs", "public/interface", "public/work", "public/exports",
                      "owner/jobs", "owner/requests", "owner/results", "owner/driver"):
        (root / directory).mkdir(parents=True, mode=0o700)
    columns = []
    for source in paths:
        rel(source.name)
        target = root / "public/inputs" / source.name
        shutil.copyfile(source, target)
        target.chmod(0o444)
        raw = target.read_bytes()
        columns.append({"name": source.name, "bytes": len(raw), "sha256": sha(target),
                        "rows": len(strings.rows(raw, row_framing)) if row_framing != "none" else None})
    driver_dir = root / "owner/driver"
    for name in ("driver.cpp", "codec.h"):
        shutil.copyfile(PROJECT / "vendor/compression_lab/data/strings" / name, driver_dir / name)
    build = ["/usr/bin/g++", "-std=c++17", "-O3", "-DNDEBUG", str(driver_dir / "driver.cpp"),
             "-ldl", "-o", str(driver_dir / "driver")]
    if server_local:build=['/usr/bin/taskset','-c',str(cpu),*build]
    compiled = subprocess.run(build, capture_output=True, text=True, timeout=None, env=_clean_env())
    save(driver_dir / "build.json", {"argv": build, "returncode": compiled.returncode,
                                    "stdout": compiled.stdout, "stderr": compiled.stderr})
    compiled.check_returncode()
    shutil.copyfile(PROJECT / "include/codec.h", root / "public/interface/codec.h")
    protocol = {
        "schema_version": 1, "interface_version": __version__, "runtime": runtime,
        "measurement_status": "development_preview_not_paper_reproduction",
        "scoring": SCORING, "columns": columns, "original_bytes": sum(c["bytes"] for c in columns),
        "native_adapter": {"abi": "strings-v1", "header": "/interface/codec.h",
                           "binary_format": "Linux x86-64 ELF shared library", "cpu": cpu,
                           "native_threads": 1, "row_framing": row_framing,
                           "inputs": "/inputs", "work": "/work"},
        "measurement": {"warmups": 1, "trials": 7, "quick_trials": 1,
                        "bulk": "one complete column; fresh setup plus complete byte reconstruction",
                        "selective": "nested shuffled subsets, sorted IDs, ceiling of percentage times row count",
                        "selectivities": list(strings.SELECTIVITIES) if row_framing != "none" else [],
                        "aggregation": "sum column times within each trial, then median and observed min/max",
                        "encode_scope": "lab_encode call only; does not certify external fitting or preprocessing costs",
                        "decode_warm_scope": "second call on the same state, reported separately",
                        "size_scope": "separate archive, custom decoder and dependency bytes; no primary score"},
        "candidate_manifest": {
            "required": ["name", "variant", "encoder", "decoder", "sources"],
            "optional": ["libraries", "description"], "variants": ["bulk", "rows"],
            "paths": "relative to manifest directory inside /work; no links or parent traversals",
            "libraries": "optional SONAME to relative shared-library path; system libraries resolved and pinned",
        },
        "research_environment": {"tools": "installed tools plus software installed in /work",
                                 "network": True, "language": "any language that can provide the C ABI",
                                 "previous_implementations": "not mounted", "precomputed_baselines": "not provided"},
    }
    protocol["protocol_id"] = digest(protocol)
    save(root / "public/interface/protocol.json", protocol, 0o444)
    config = {"schema_version": 1, "created_utc": now(), "cpu": cpu, "row_framing": row_framing,
              "runtime": runtime, "columns": columns, "protocol": protocol,
              "server_local": server_local, "build_cpu": cpu,
              "proot": str(proot_path()) if server_local else None,
              "public_pins": {"interface/codec.h": sha(root / "public/interface/codec.h"),
                              "interface/protocol.json": sha(root / "public/interface/protocol.json")},
              "driver": {name: sha(driver_dir / name) for name in ("driver", "driver.cpp", "codec.h")},
              "host_lock": str(Path(host_lock or "/tmp/compression-lab-host-benchmark-v1.lock").absolute())}
    save(root / "owner/config.json", config, 0o400)
    return Lab(root)


class Lab:
    def __init__(self, root):
        self.root = Path(root).resolve(strict=True)
        self.owner = self.root / "owner"
        self.public = self.root / "public"
        self.config = load(self.owner / "config.json")

    def environment(self):
        for name,expected in self.config.get('interface_pins',{}).items():
            if sha(PROJECT/name)!=expected:raise Error('interface_runtime_changed')
        for column in self.config["columns"]:
            path = self.public / "inputs" / column["name"]
            if path.is_symlink() or sha(path) != column["sha256"]:
                raise Error("input_changed", column["name"])
        for name, expected in self.config["public_pins"].items():
            path = self.public / name
            if path.is_symlink() or sha(path) != expected:
                raise Error("interface_changed", name)
        for name, expected in self.config["driver"].items():
            if sha(self.owner / "driver" / name) != expected:
                raise Error("driver_changed", name)
        return self.config["protocol"]

    def shell_command(self, argv):
        """A research shell can compile/install in /work but cannot see owner files.

        This is filesystem isolation, not a restriction on independently searching
        public algorithms. Network access is intentional. Do not run an agent with
        an additional unrestricted host shell and claim that it is isolated.
        """
        self.environment()
        if not argv or any(not isinstance(a, str) or "\0" in a for a in argv):
            raise Error("invalid_argv")
        if self.config.get('server_local'):
            from .server_shell import command
            return command(self.owner/'shell-policies',argv,
                           {'/'+name:self.public/name for name in ('inputs','interface','work','exports')},
                           writable=['/work'],cwd='/work',cpus=[self.config['build_cpu']],
                           proot=self.config['proot'])
        bwrap = shutil.which("bwrap")
        if not bwrap:
            raise Error("research_sandbox_unavailable")
        args = [bwrap, "--die-with-parent", "--new-session", "--unshare-user", "--unshare-pid",
                "--unshare-ipc", "--unshare-uts", "--cap-drop", "ALL", "--clearenv",
                "--ro-bind", "/usr", "/usr"]
        for name in ("/bin", "/sbin", "/lib", "/lib64"):
            p = Path(name)
            if p.is_symlink():
                args += ["--symlink", os.readlink(p), name]
            elif p.exists():
                args += ["--ro-bind", name, name]
        for name in ("/etc/resolv.conf", "/etc/hosts", "/etc/nsswitch.conf", "/etc/ssl/certs",
                     "/etc/ld.so.cache", "/etc/passwd", "/etc/group"):
            if Path(name).exists():
                args += ["--ro-bind", str(Path(name).resolve()), name]
        for name, destination, writable in (("inputs", "/inputs", False),
                                            ("interface", "/interface", False),
                                            ("work", "/work", True), ("exports", "/exports", False)):
            args += ["--bind" if writable else "--ro-bind", str(self.public / name), destination]
        args += ["--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--dir", "/home/researcher",
                 "--setenv", "HOME", "/work", "--setenv", "PATH", "/work/bin:/usr/local/bin:/usr/bin:/bin",
                 "--setenv", "LANG", "C.UTF-8", "--chdir", "/work", "--", *argv]
        return args

    def submit(self, manifest, *, request_id, quick=False):
        """Freeze candidate and start one durable job; request IDs prevent retries duplicating work."""
        ident(request_id)
        rel(manifest)
        self.environment()
        if (self.owner/'FINISH.json').exists():raise Error('workspace_finished')
        if shutil.disk_usage(self.root).free < 6*1024**3:raise Error('local_disk_reserve')
        request = {"manifest": manifest, "quick": bool(quick)}
        receipt_path = self.owner / "requests" / (request_id + ".json")
        with lock(self.owner / "submit.lock"):
            if receipt_path.exists():
                receipt = load(receipt_path)
                if receipt["request"] != request:
                    raise Error("request_id_conflict")
                return self.status(receipt["job_id"])
            job_id = "j-" + uuid.uuid4().hex
            job = self.owner / "jobs" / job_id
            job.mkdir()
            try:
                self._snapshot(manifest, job)
            except BaseException:
                # Keep failed imports private for diagnosis; never launch a partial snapshot.
                save(job / "status.json", {"job_id": job_id, "status": "failed", "reason": "snapshot_failed"})
                raise
            state = {"job_id": job_id, "status": "queued", "created_utc": now(),
                     "quick": bool(quick), "kind": "measurement", "request_id": request_id}
            save(receipt_path, {"request": request, "job_id": job_id})
            self._launch(state)
            return self.status(job_id)

    def _launch(self, state):
        job = self.owner / "jobs" / state["job_id"]
        with lock(job / "state.lock"):
            save(job / "status.json", state)
            try:
                with (job / "worker.log").open("wb") as log:
                    worker = subprocess.Popen([sys.executable, "-m", "lab_interface.worker", str(self.root), state["job_id"]],
                                              stdout=log, stderr=log, stdin=subprocess.DEVNULL,
                                              start_new_session=True, close_fds=True, env=_clean_env())
                state.update(pid=worker.pid, process_start=_identity(worker.pid))
                # Keep the Popen object alive until reaped without blocking MCP.
                threading.Thread(target=worker.wait, daemon=True).start()
                save(job / "status.json", state)
            except OSError:
                state.update(status="failed", reason="worker_launch_failed", ended_utc=now())
                save(job / "status.json", state)
                raise

    def profile(self, result_id, *, request_id):
        """Separate diagnostic replay of an already frozen, byte-exact candidate."""
        ident(request_id)
        row = self.result(result_id)
        if not result_id.startswith("r-") or not row["correctness"]["byte_exact"]:
            raise Error("verified_measurement_required")
        request = {"kind": "profile", "result_id": result_id}
        receipt_path = self.owner / "requests" / (request_id + ".json")
        with lock(self.owner / "submit.lock"):
            if receipt_path.exists():
                receipt = load(receipt_path)
                if receipt["request"] != request:
                    raise Error("request_id_conflict")
                return self.status(receipt["job_id"])
            job_id = "j-" + uuid.uuid4().hex
            (self.owner / "jobs" / job_id).mkdir()
            state = {"job_id": job_id, "kind": "profile", "status": "queued", "created_utc": now(),
                     "source_result_id": result_id, "request_id": request_id}
            save(receipt_path, {"request": request, "job_id": job_id})
            self._launch(state)
            return self.status(job_id)

    def build(self, manifest, *, request_id):
        from .workflow import queue_build
        return queue_build(self, manifest, request_id)

    def evaluate(self, build_id, *, request_id, quick=False):
        if 'dbtext' in self.config['protocol']:
            raise Error('use_evaluate_dbtext')
        from .workflow import queue_evaluation
        return queue_evaluation(self, build_id, request_id, quick)

    def evaluate_dbtext(self, build_id, *, request_id, quick=False):
        """One immutable package, bulk reconstruction and paper-style selected-row queries."""
        if 'dbtext' not in self.config['protocol']:
            raise Error('dbtext_protocol_not_sealed')
        from .workflow import queue_evaluation
        return queue_evaluation(self, build_id, request_id, quick, dbtext=True)

    def validate(self, result_id, *, request_id):
        from .workflow import queue_validation
        return queue_validation(self, result_id, request_id)

    def jobs(self):
        return {"jobs": [self.status(p.parent.name) for p in sorted((self.owner / "jobs").glob("*/status.json"))]}

    def cancel(self, job_id):
        from .workflow import cancel
        return cancel(self, job_id)

    def artifact(self, result_id, path=None, *, offset=0, limit=32768):
        from .workflow import artifact
        return artifact(self, result_id, path, offset, limit)

    def compare(self, result_ids):
        from .workflow import compare
        return compare(self, result_ids)

    def finish(self, result_id, validation_id, *, request_id):
        from .workflow import finish
        return finish(self, result_id, validation_id, request_id)

    def _snapshot(self, name, job, *, source_root=None, manifest_data=None):
        work = self.public / "work"
        manifest = secure_load(work, name) if manifest_data is None else manifest_data
        required = {"name", "variant", "encoder", "decoder", "sources"}
        if not isinstance(manifest, dict) or not required <= manifest.keys() or set(manifest) - required - {"libraries", "description"}:
            raise Error("invalid_manifest")
        ident(manifest["name"])
        if manifest["variant"] not in ("bulk", "rows") or not isinstance(manifest["sources"], list) or not manifest["sources"]:
            raise Error("invalid_manifest")
        libraries = manifest.get("libraries", {})
        if not isinstance(libraries, dict):
            raise Error("invalid_libraries")
        for soname in libraries:
            if not re.fullmatch(r"[A-Za-z0-9_+.-]+", soname):
                raise Error("invalid_soname")
        files = [manifest["encoder"], manifest["decoder"], *manifest["sources"], *libraries.values()]
        if len(files) > 20000 or "engine-manifest.json" in files:
            raise Error("invalid_candidate_files")
        source_root = source_root or work / Path(name).parent
        snapshot = job / "candidate"
        snapshot.mkdir()
        inventory = {}
        for relative in sorted(set(rel(p) for p in files)):
            target = snapshot / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            candidate.copy_import(source_root, relative, target)
            target.chmod(0o444)
            inventory[relative] = {"bytes": target.stat().st_size, "sha256": sha(target)}
        if sum(p["bytes"] for p in inventory.values()) > 512 * 1024**2:
            raise Error("oversized_candidate")
        save(job / "submission.json", {"manifest": manifest, "files": inventory}, 0o444)
        save(job / "candidate-manifest.json", {k: manifest[k] for k in ("name", "variant", "encoder", "decoder")}, 0o444)
        # Engine manifest paths are relative to itself, so retain it in the snapshot.
        save(snapshot / "engine-manifest.json", load(job / "candidate-manifest.json"), 0o444)
        runtime = job / "libraries"
        runtime.mkdir()
        known = candidate.libraries()
        known.update({soname: snapshot / rel(relative) for soname, relative in libraries.items()})
        pending = [snapshot / manifest[role] for role in ("encoder", "decoder")]
        pending.append(self.owner / "driver/driver")
        pinned = {}
        while pending:
            info = candidate.elf(pending.pop())
            for soname in info["needed"]:
                if soname in pinned:
                    continue
                if soname not in known or Path(soname).name != soname:
                    raise Error("dependency_unavailable", soname)
                target = runtime / soname
                shutil.copyfile(known[soname], target)
                target.chmod(0o444)
                pinned[soname] = {"path": str(target), "sha256": sha(target), "bytes": target.stat().st_size,
                                  "standard_codec": False, "classification": "unclassified_pending_policy"}
                pending.append(target)
        save(job / "libraries.json", pinned, 0o444)

    def status(self, job_id):
        ident(job_id)
        job = self.owner / "jobs" / job_id
        if not (job / "status.json").is_file():
            raise Error("unknown_job")
        with lock(job / "state.lock"):
            state = load(job / "status.json")
            if state["status"] in ACTIVE and state.get("process_start") and _identity(state["pid"]) != state["process_start"]:
                state.update(status="interrupted", reason="worker_exited_without_terminal_receipt", ended_utc=now())
                save(job / "status.json", state)
            return {k: v for k, v in state.items() if k not in ("pid", "process_start")}

    def result(self, result_id):
        if not re.fullmatch(r"[rpbv]-[a-f0-9]{64}", result_id):
            raise Error("invalid_result_id")
        row = load(self.owner / "results" / (result_id + ".json"))
        if result_id[:2] + digest({k: v for k, v in row.items() if k != "result_id"}) != result_id:
            raise Error("result_changed")
        job = self.owner / "jobs" / row["job_id"]
        if sha(job / "retained-files.json") != row["evidence_manifest_sha256"]:
            raise Error("evidence_manifest_changed")
        for name, expected in load(job / "retained-files.json").items():
            if sha(job / rel(name)) != expected:
                raise Error("retained_evidence_changed", name)
        return row

    def execute(self, argv):
        """Research command with no runtime cap, host shell or inherited credentials."""
        command = self.shell_command(argv)
        with tempfile.TemporaryDirectory(prefix="research-command-", dir=self.owner) as directory:
            folder = Path(directory)
            with (folder / "stdout").open("wb") as out, (folder / "stderr").open("wb") as err:
                proc = subprocess.Popen(command, stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                                        env=_clean_env(), start_new_session=True, close_fds=True)
                reason = None
                try:
                    while proc.poll() is None:
                        if out.tell() + err.tell() > 16 * 1024**2:
                            reason = "output_limit"
                        if reason:
                            os.killpg(proc.pid, signal.SIGKILL)
                            break
                        time.sleep(0.03)
                finally:
                    if proc.poll() is None:
                        try:
                            os.killpg(proc.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    proc.wait()
            data = {name: (folder / name).read_bytes()[:32768].decode("utf-8", "replace") for name in ("stdout", "stderr")}
            return {"returncode": proc.returncode, "reason": reason, **data,
                    "truncated": any((folder / name).stat().st_size > 32768 for name in data)}

    def export(self, result_id):
        row = self.result(result_id)
        job = self.owner / "jobs" / row["job_id"]
        target = self.public / "exports" / (result_id + ".zip")
        with lock(job / "export.lock"):
            if target.exists():
                expected = load(job / "export.json")
                if sha(target) != expected["sha256"]:
                    raise Error("export_changed")
                return expected
            temporary = target.with_suffix(".tmp")
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("result.json", (self.owner / "results" / (result_id + ".json")).read_bytes())
                archive.writestr("protocol.json", self.public.joinpath("interface/protocol.json").read_bytes())
                archive.writestr("interface/codec.h", self.public.joinpath("interface/codec.h").read_bytes())
                archive.writestr("retained-files.json", (job / "retained-files.json").read_bytes())
                for relative in load(job / "retained-files.json"):
                    archive.write(job / relative, relative)
            temporary.chmod(0o444)
            temporary.replace(target)
            receipt = {"relative_path": "exports/" + target.name, "sha256": sha(target), "bytes": target.stat().st_size}
            save(job / "export.json", receipt)
            return receipt
