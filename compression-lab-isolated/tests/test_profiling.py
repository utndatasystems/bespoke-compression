import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from lab_interface.profiling import parse_counters, replay_command
from compression_lab import strings
from compression_lab.util import load, sha
import test_interface


class CounterParsingTests(unittest.TestCase):
    def test_unavailable_counters_and_multiplexing_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "counters.csv"
            p.write_text("12;;cycles:u;200;50.00;;\n<not supported>;;cache-misses:u;0;100.00;;\n")
            rows = parse_counters(p)
            self.assertEqual(rows[0]["value"], 12)
            self.assertEqual(rows[0]["time_running_percent"], 50)
            self.assertIsNone(rows[1]["value"])
            self.assertEqual(rows[1]["counter_status"], "<not supported>")


class ProfileSandboxTests(unittest.TestCase):
    def test_exact_replay_without_needing_hardware_counter_permissions(self):
        fixture = test_interface.InterfaceTests("test_exact_rows_snapshots_idempotency_and_export")
        fixture.setUp()
        try:
            lab = fixture.lab
            state = fixture.wait(lab.submit(fixture.build_copy(), request_id="profile-sandbox", quick=True))
            row = lab.result(state["result_id"])
            job = lab.owner / "jobs" / row["job_id"]
            config = strings.config(job / "engine")
            column = lab.config["columns"][0]
            decoder = job / "candidate/decoder.so"
            archive = job / "archives" / (column["sha256"] + ".bin")
            out = job / "diagnostic-output"
            with replay_command(config, decoder, archive, out, column["bytes"] + 32) as (argv, fd):
                run = subprocess.run(["taskset", "-c", str(lab.config["cpu"]), *argv], pass_fds=(fd,),
                                     capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(sha(out / "data"), column["sha256"])
        finally:
            fixture.doCleanups()
