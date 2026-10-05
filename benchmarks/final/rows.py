"""Portable launcher for the unchanged final Lab DBText query driver and checks."""
import json
import os
from pathlib import Path
import resource
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'compression-lab-isolated/src'))
from lab_interface import dbtext
from compression_lab.util import sha

def execute(cfg, driver, decoder, archive, column, output, mode, **unused):
    output.mkdir(parents=True, exist_ok=False)
    argv = [str(driver), str(decoder), str(archive), str(column['rows']), str(column['bytes']), str(output), mode]
    def limits():
        os.sched_setaffinity(0, {cfg['cpu']})
        resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3,) * 2)
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run(argv, capture_output=True, text=True, preexec_fn=limits, timeout=None)
    (output / 'execution.json').write_text(json.dumps({'argv': argv, 'returncode': result.returncode,
        'stdout': result.stdout, 'stderr': result.stderr, 'cpu': cfg['cpu'], 'filesystem_sandbox': False}, indent=2) + '\n')
    result.check_returncode()
    return json.loads(result.stdout)

def measure(data, columns, folder, decoder, driver, cpu):
    # Archives were produced and verified by the frozen final bulk driver.
    config = {'cpu': cpu, 'columns': [{**c, 'path': str(data / c['name']),
        'rows': (data / c['name']).read_bytes().count(b'\n')} for c in columns]}
    native = {'evidence': str(folder), 'candidate': {'decoder': sha(decoder)}}
    dbtext.execute = execute
    return dbtext.measure(config, native, driver, quick=False, remote=False)
