"""Host receipts and the shared lease for portable benchmark entry points.

These helpers run outside the native timers. They describe a measurement;
correctness checks and aggregation remain in each workload's evaluator.
"""
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import platform
import subprocess

from . import __version__, runner
from .util import Error, digest, sha


def host_identity():
    """Record hardware/runtime details without publishing the hostname."""
    cpu = platform.processor() or platform.machine()
    cpuinfo = Path('/proc/cpuinfo')
    if cpuinfo.exists():
        cpu = next((line.split(':', 1)[1].strip() for line in cpuinfo.read_text().splitlines()
                    if line.startswith('model name')), cpu)
    machine = Path('/etc/machine-id')
    identity = machine.read_text().strip() if machine.exists() else platform.node()
    host = dict(system=platform.system(), kernel=platform.release(), architecture=platform.machine(),
                cpu_model=cpu, logical_cpus=os.cpu_count(), python=platform.python_version())
    return {'machine_id': digest({'host': host, 'installation': identity}), **host}


def write_metadata(out, *, protocol, inputs, cpu, smoke=False, parameters=None,
                   artifacts=(), dataset=None):
    """Write one immutable receipt before timing, pinning inputs and built files.

    ``inputs`` maps logical column names to files. ``parameters`` includes every
    timing choice (passes, setup boundary, aggregation, framing, resource limits).
    Separate summaries from the same run may reuse this receipt for reporting.
    """
    out = Path(out)
    if not inputs:
        raise ValueError('At least one input is required')
    if cpu not in os.sched_getaffinity(0):
        raise ValueError('Selected CPU is outside the process affinity')
    pins = [{'name': name, 'bytes': Path(path).stat().st_size, 'sha256': sha(Path(path))}
            for name, path in inputs.items()]
    host = host_identity()
    settings = {'cpu': cpu, 'threads': 1, **(parameters or {})}
    files = []
    for path in sorted({Path(p).resolve() for p in artifacts}):
        # Labels describe files; their hashes, not host paths, identify builds.
        files.append({'name': path.name, 'bytes': path.stat().st_size, 'sha256': sha(path)})
    try:
        compiler = subprocess.check_output(['g++', '--version'], text=True, timeout=None).splitlines()[0]
    except (OSError, subprocess.SubprocessError):
        compiler = None
    receipt = dict(schema_version=1, toolkit_version=__version__, protocol=protocol,
                   machine_id=host['machine_id'], machine_label=host['cpu_model'], host=host,
                   workload_id=digest(pins), dataset=dataset or ' / '.join(inputs),
                   original_bytes=sum(pin['bytes'] for pin in pins), inputs=pins,
                   parameters=settings, smoke=bool(smoke), artifacts=files, compiler=compiler,
                   created_utc=datetime.now(timezone.utc).isoformat())
    receipt['comparison_id'] = digest({k: receipt[k] for k in
        ('machine_id', 'protocol', 'workload_id', 'parameters', 'smoke')})
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'run-metadata.json').open('x') as stream:
        json.dump(receipt, stream, indent=2)
        stream.write('\n')
    return receipt


@contextmanager
def measurement_lock():
    """Use the same nonblocking host lease as native Lab evaluations."""
    with runner.HOST_LOCK.open('a') as lease:
        try:
            fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise Error('benchmark_lease_busy', 'Another benchmark is measuring on this host; retry after it finishes.') from error
        yield
