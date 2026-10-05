"""Integration checks for measurement integrity and the researcher boundary."""
import json
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import zipfile

try:
    from lab_interface.api import Lab, initialize
except ImportError:
    Lab = initialize = None


COPY_CODEC = r'''
#include "codec.h"
#include <cstring>
#include <vector>
#ifndef DECODER
extern "C" int64_t lab_encode(const uint8_t* p,size_t n,uint8_t* o,size_t c){
 if(n>c)return -1;memcpy(o,p,n);return n;
}
#else
struct State{const uint8_t* p;size_t n;};
extern "C" void* lab_open(const uint8_t*p,size_t n){return new State{p,n};}
extern "C" void lab_close(void*s){delete (State*)s;}
extern "C" int64_t lab_decode(void*v,uint8_t*o,size_t c){
 auto&s=*(State*)v;if(s.n>c)return -1;memcpy(o,s.p,s.n);return s.n;
}
extern "C" int64_t lab_rows(void*v,const uint64_t*ids,size_t count,uint8_t*o,size_t c,uint64_t*offsets){
 auto&s=*(State*)v;std::vector<size_t> ends{0};
 for(size_t i=0;i<s.n;++i)if(s.p[i]=='\n')ends.push_back(i+1);
 if(ends.back()!=s.n)ends.push_back(s.n);
 size_t n=0;offsets[0]=0;
 for(size_t i=0;i<count;++i){if(ids[i]+1>=ends.size())return -1;
  size_t a=ends[ids[i]],b=ends[ids[i]+1];if(b-a>c-n)return -1;
  memcpy(o+n,s.p+a,b-a);n+=b-a;offsets[i+1]=n;
 }return n;
}
#endif
'''


class InterfaceTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(Lab, "The isolated Lab interface is not implemented")
        self.temp = tempfile.TemporaryDirectory(prefix="lab-interface-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / "strings"
        self.input.write_bytes(b"alpha\n\nbeta\ntail")
        self.lab = initialize(self.root / "lab", [self.input], host_lock=self.root / "host-benchmark.lock", cpu=min(os.sched_getaffinity(0)), server_local=os.environ.get("LAB_TEST_SERVER_LOCAL")=="1")

    def build_copy(self, corrupt=False):
        folder = self.lab.root / "public/work/copy"
        folder.mkdir()
        code = COPY_CODEC.replace("memcpy(o,s.p,s.n);return s.n;", "memset(o,0,s.n);return s.n;") if corrupt else COPY_CODEC
        (folder / "copy.cpp").write_text(code)
        for role in ("encoder", "decoder"):
            args = ["g++", "-std=c++17", "-O2", "-fPIC", "-shared", "-I/interface", "/work/copy/copy.cpp", "-o", f"/work/copy/{role}.so"]
            if role == "decoder":
                args.append("-DDECODER")
            run = subprocess.run(self.lab.shell_command(args), capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
        (folder / "manifest.json").write_text(json.dumps({
            "name": "copy", "variant": "rows", "encoder": "encoder.so", "decoder": "decoder.so",
            "sources": ["copy.cpp"], "libraries": {}, "description": "Uncompressed test control."
        }))
        return "copy/manifest.json"

    def wait(self, job):
        deadline = time.monotonic() + 75
        while time.monotonic() < deadline:
            state = self.lab.status(job["job_id"])
            if state["status"] not in ("queued", "running", "cancelling"):
                return state
            time.sleep(0.1)
        self.fail("Job did not become terminal")

    def test_researcher_cannot_read_owner_or_host_files(self):
        secret = self.lab.root / "owner/private-baselines.json"
        secret.write_text('{"private_test_secret":true}')
        host_secret = self.root / "host-secret"
        host_secret.write_text("secret")
        code = "import pathlib; assert not pathlib.Path(%r).exists(); assert not pathlib.Path(%r).exists(); assert pathlib.Path('/inputs/strings').read_bytes()==b'alpha\\n\\nbeta\\ntail'" % (str(secret), str(host_secret))
        r = subprocess.run(self.lab.shell_command(["python3", "-c", code]), capture_output=True, text=True, timeout=10)
        self.assertEqual(r.returncode, 0, r.stderr)
        r = subprocess.run(self.lab.shell_command(["python3", "-c", "open('/inputs/strings','wb').write(b'changed')"]), capture_output=True, timeout=10)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(self.input.read_bytes(), b"alpha\n\nbeta\ntail")
        self.assertFalse(list((self.lab.root / "public").rglob("*.md")))
        self.assertNotIn("private_test_secret", json.dumps(self.lab.environment()))
        self.assertEqual(self.lab.environment()["scoring"]["status"], "pending_owner_decision")

    def test_exact_rows_snapshots_idempotency_and_export(self):
        manifest = self.build_copy()
        with open(self.lab.config["host_lock"], "a") as lease:
            fcntl.flock(lease, fcntl.LOCK_EX)
            job = self.lab.submit(manifest, request_id="copy-check", quick=True)
            duplicate = self.lab.submit(manifest, request_id="copy-check", quick=True)
            self.assertEqual(job["job_id"], duplicate["job_id"])
            time.sleep(0.3)
            self.assertEqual(self.lab.status(job["job_id"])["status"], "queued")
            # Edits while queued must not alter the candidate being measured.
            (self.lab.root / "public/work/copy/decoder.so").write_bytes(b"changed after submission")
            fcntl.flock(lease, fcntl.LOCK_UN)
        state = self.wait(job)
        self.assertEqual(state["status"], "complete", state)
        result = self.lab.result(state["result_id"])
        self.assertTrue(result["correctness"]["byte_exact"])
        self.assertEqual(result["sizes"]["archive_bytes"], 16)
        self.assertGreater(result["sizes"]["custom_decoder_bytes"], 0)
        self.assertEqual(result["scoring"]["status"], "pending_owner_decision")
        self.assertIsNone(result["scoring"]["primary_score"])
        self.assertEqual(set(result["measurements"]["selective"]), {"1", "3", "10", "30", "100"})
        self.assertNotIn("references", result)
        export = self.lab.export(state["result_id"])
        with zipfile.ZipFile(self.lab.root / "public" / export["relative_path"]) as archive:
            self.assertIn("result.json", archive.namelist())
            self.assertTrue(any(p.endswith("copy.cpp") for p in archive.namelist()))
            self.assertFalse(any("private-baselines" in p for p in archive.namelist()))

    def test_full_bulk_protocol_keeps_one_warmup_seven_trials_and_cpu_pins(self):
        path = self.build_copy()
        file = self.lab.public / "work" / path
        manifest = json.loads(file.read_text())
        manifest["variant"] = "bulk"
        file.write_text(json.dumps(manifest))
        state = self.wait(self.lab.submit(path, request_id="full-bulk", quick=False))
        self.assertEqual(state["status"], "complete", state)
        row = self.lab.result(state["result_id"])
        self.assertEqual(row["depth"], "full")
        self.assertEqual(row["measurements"]["selective"], {})
        raw = self.lab.owner / "jobs" / state["job_id"] / "trials.jsonl"
        trials = [json.loads(line) for line in raw.read_text().splitlines()]
        self.assertEqual([t["trial"] for t in trials], list(range(8)))
        for trial in trials:
            self.assertTrue(trial["exact"])
            for operation in ("encode", "decode"):
                resource = trial["resources"][operation]["resources"]
                self.assertEqual(resource["cpus"], [self.lab.config["cpu"]])
                self.assertIn("seccomp", resource["enforcement"]["affinity"])

    def test_wrong_decoder_is_not_accepted(self):
        state = self.wait(self.lab.submit(self.build_copy(corrupt=True), request_id="bad-copy", quick=True))
        self.assertEqual(state["status"], "failed", state)
        self.assertFalse(self.lab.result(state["result_id"])["correctness"]["byte_exact"])

    def test_path_escape_and_modified_inputs_are_rejected(self):
        with self.assertRaises(ValueError):
            self.lab.submit("../../owner/secret.json", request_id="escape", quick=True)
        p = self.lab.root / "public/inputs/strings"
        p.chmod(0o644)
        p.write_bytes(b"modified")
        with self.assertRaises(Exception):
            self.lab.environment()

    def test_receipts_detect_tampering_and_request_conflicts(self):
        manifest = self.build_copy()
        job = self.lab.submit(manifest, request_id="stable", quick=True)
        with self.assertRaises(ValueError):
            self.lab.submit(manifest, request_id="stable", quick=False)
        state = self.wait(job)
        result = self.lab.result(state["result_id"])
        receipt = self.lab.root / "owner/jobs" / result["job_id"] / "retained-files.json"
        receipt.chmod(0o644)
        receipt.write_text("{}")
        with self.assertRaises(ValueError):
            self.lab.export(state["result_id"])

    def test_profiler_preserves_unavailable_counters_and_candidate_identity(self):
        state = self.wait(self.lab.submit(self.build_copy(), request_id="profile-source", quick=True))
        measured = self.lab.result(state["result_id"])
        job = self.lab.profile(state["result_id"], request_id="profile-once")
        self.assertEqual(job["job_id"], self.lab.profile(state["result_id"], request_id="profile-once")["job_id"])
        finished = self.wait(job)
        self.assertIn(finished["status"], {"complete", "unavailable"}, finished)
        result = self.lab.result(finished["result_id"])
        self.assertEqual(result["source_result_id"], measured["result_id"])
        self.assertEqual(result["candidate"], measured["candidate"])
        self.assertEqual(result["scoring"]["status"], "pending_owner_decision")
        if finished["status"] == "unavailable":
            self.assertIsNone(result["counters"])
            self.assertTrue(result["reason"])

    def test_research_command_has_no_runtime_cap_or_inherited_secret(self):
        os.environ["LAB_TEST_SECRET"] = "private"
        try:
            output = self.lab.execute(["python3", "-c", "import os; print(os.getenv('LAB_TEST_SECRET'))"])
        finally:
            del os.environ["LAB_TEST_SECRET"]
        self.assertEqual(output["stdout"].strip(), "None")
        self.assertIsNone(self.lab.execute(["sleep", "1"])["reason"])


if __name__ == "__main__":
    unittest.main()
