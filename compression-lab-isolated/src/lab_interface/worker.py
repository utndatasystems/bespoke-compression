"""Detached, credential-free worker. No model calls, retries or paid continuations."""
from pathlib import Path
import shutil
import signal
import sys

from .api import Lab, SCORING, verify_runtime
from compression_lab import runner, strings
from compression_lab.util import digest, ident, load, lock, now, save, sha


def measure(root, job_id):
    ident(job_id)
    lab = Lab(root)
    job = lab.owner / "jobs" / job_id
    with lock(job / "state.lock"):
        state = load(job / "status.json")
    def cancelled(*_):
        raise InterruptedError('cancelled')
    signal.signal(signal.SIGTERM, cancelled)
    try:
        verify_runtime()
        lab.environment()
        if state['kind'] in ('build', 'validation'):
            from .workflow import run_build, run_validation
            with lock(job/'state.lock'):
                state.update(status='running',started_utc=now())
                save(job/'status.json',state)
            row=(run_build if state['kind']=='build' else run_validation)(lab,job,state)
            passed=row['reproducible'] if state['kind']=='build' else row['passed']
            state.update(status='complete' if passed else 'failed',result_id=row['result_id'],ended_utc=now())
            with lock(job/'state.lock'):save(job/'status.json',state)
            return
        if state["kind"] == "profile":
            from .profiling import run
            row = run(lab, job, state)
            retained = {str(p.relative_to(job)): sha(p) for p in (job / "profile").rglob("*") if p.is_file()}
            save(job / "retained-files.json", retained, 0o444)
            row["evidence_manifest_sha256"] = sha(job / "retained-files.json")
            row["result_id"] = "p-" + digest(row)
            save(lab.owner / "results" / (row["result_id"] + ".json"), row, 0o444)
            state.update(status=row["status"], result_id=row["result_id"], ended_utc=now())
            with lock(job / "state.lock"):
                save(job / "status.json", state)
            return
        submission = load(job / "submission.json")
        for name, expected in submission["files"].items():
            if sha(job / "candidate" / name) != expected["sha256"]:
                raise ValueError("snapshot_changed")
        engine = job / "engine"
        driver = lab.owner / "driver"
        strings.init(engine, [(c["name"], lab.public / "inputs" / c["name"]) for c in lab.config["columns"]],
                     driver / "driver", load(job / "libraries.json"), lab.config["cpu"],
                     row_framing=lab.config["row_framing"],
                     driver_sources={name: driver / name for name in ("driver.cpp", "codec.h")})
        config = load(engine / "strings.json")
        config.update(primary_objectives=[], compression_speed="unscored development measurements")
        if lab.config.get('server_local'):config['server_local']=True
        save(engine / "strings.json", config, 0o444)
        # Configuration adapter: the retained campaign runtime otherwise names
        # its historical server lock. Its source remains byte-for-byte unchanged.
        runner.HOST_LOCK = Path(lab.config["host_lock"])
        runner.HOST_LOCK.parent.mkdir(parents=True, exist_ok=True)
        def native_run(*args):
            # The engine invokes this only after acquiring its benchmark lease.
            if state["status"] == "queued":
                with lock(job / "state.lock"):
                    state.update(status="running", started_utc=now())
                    save(job / "status.json", state)
            if lab.config.get('server_local'):
                from .remote_worker import native_execute
                return native_execute(*args)
            return strings._run(*args)
        manifest = job/'candidate/engine-manifest.json'
        if state.get('dbtext'):
            from .dbtext import bulk_manifest
            manifest = bulk_manifest(manifest)
        native = strings.evaluate(engine, manifest, state["quick"],
                                  run=native_run, wait_for_lease=True) if not lab.config.get('remote') else None
        if lab.config.get('remote'):
            shutil.copytree(driver,job/'harness',ignore=shutil.ignore_patterns('build.json'))
            from .remote import measure as remote_measure
            native=remote_measure(lab,job,state)
        elif state.get('dbtext'):
            from .dbtext import attach
            native=attach(strings.config(engine),native,driver/'dbtext_rows',quick=state['quick'],
                          remote=lab.config.get('server_local',False))
        supplied_libraries = submission["manifest"].get("libraries", {})
        dependencies = []
        for dependency in native["dependencies"]:
            item = {k: dependency[k] for k in ("soname", "sha256", "bytes", "platform")}
            item["origin"] = "candidate" if item["soname"] in supplied_libraries else "system"
            # A candidate's library is not a platform component merely because
            # its author chose the SONAME of a platform library.
            item["platform"] = item["platform"] and item["origin"] == "system"
            dependencies.append(item)
        row = {"schema_version": 1, "job_id": job_id, "name": native["name"], "variant": native["variant"],
               "protocol_id": lab.config["protocol"]["protocol_id"], "runtime": lab.config["runtime"],
               "depth": native["depth"], "scoring": lab.config['protocol']['scoring'],
               "build_id": state.get('build_id'),
               "correctness": {"byte_exact": native["quality_passed"],
                               "scope": "full reconstruction and requested rows on the supplied inputs",
                               "qualification": "development check; not a source audit or paper benchmark certification"},
               "candidate": {"files": submission["files"], "binaries": native["candidate"]},
               "original_bytes": native["original_bytes"],
               "sizes": {k: native.get("accounting", {}).get(k) for k in
                         ("archive_bytes", "custom_decoder_bytes", "nonplatform_dependency_bytes")},
               "dependencies": dependencies,
               "measurements": {"bulk": native.get("timing", {}), "selective": native.get("selective", {}),
                                "columns": native["columns"]},
               "failure": None if native["quality_passed"] else
                   {"code": native.get("reason_code"), "context": native.get("failure_context", {}),
                    "execution": native.get("failed_execution")},
               "completed_utc": now()}
        if 'dbtext' in native:
            row['measurements']['dbtext']=native['dbtext']
        row["sizes"]["nonplatform_dependency_bytes"] = sum(d["bytes"] for d in dependencies if not d["platform"])
        if lab.config['protocol']['scoring'].get('status') == 'sealed':
            from .scoring import score
            row['scoring'] = score(row, lab.config['protocol']['scoring'])
        evidence = Path(native["evidence"])
        if (evidence / "trials.jsonl").exists():
            shutil.copyfile(evidence / "trials.jsonl", job / "trials.jsonl")
        if (evidence / "archives").exists():
            shutil.copytree(evidence / "archives", job / "archives")
        if (evidence/'dbtext').exists():
            shutil.copytree(evidence/'dbtext',job/'dbtext')
        shutil.copytree(driver, job / "harness", ignore=shutil.ignore_patterns("build.json"),dirs_exist_ok=True)
        files = [job / "submission.json", *(job / "candidate").rglob("*"), *(job / "libraries").rglob("*")]
        files += [job / "trials.jsonl", *(job / "archives").glob("*")] if (job / "archives").exists() else [job / "trials.jsonl"]
        files += list((job / "harness").iterdir())
        if (job/'dbtext').exists():
            files += [p for p in (job/'dbtext').rglob('*') if p.is_file()]
        if (job/'remote-result').exists():
            files += [p for p in (job/'remote-result').rglob('*') if p.is_file()]
        retained = {str(p.relative_to(job)): sha(p) for p in files if p.is_file()}
        save(job / "retained-files.json", retained, 0o444)
        row["evidence_manifest_sha256"] = sha(job / "retained-files.json")
        row["result_id"] = "r-" + digest(row)
        save(lab.owner / "results" / (row["result_id"] + ".json"), row, 0o444)
        state.update(status="complete" if native["quality_passed"] else "failed", result_id=row["result_id"], ended_utc=now())
    except Exception as error:
        # Full infrastructure traceback stays private; a bounded code is enough
        # to distinguish an interrupted test from a valid algorithm result.
        import traceback
        traceback.print_exc()
        if hasattr(error,'measurement'):
            state['diagnostic']={k:error.measurement.get(k) for k in ('returncode','reason_code','stdout','stderr')}
        state.update(status="cancelled" if isinstance(error,InterruptedError) or (job/'cancel').exists() else "failed",
                     reason=getattr(error, "code", type(error).__name__), ended_utc=now())
    with lock(job / "state.lock"):
        save(job / "status.json", state)


if __name__ == "__main__":
    measure(Path(sys.argv[1]), sys.argv[2])
