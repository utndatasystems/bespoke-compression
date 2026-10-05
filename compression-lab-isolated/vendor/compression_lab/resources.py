"""Validated native allocations and stable physical CPU identity."""
import os
from pathlib import Path
from .util import Error, digest

THREAD_ENV = ('COMPRESSION_LAB_THREADS', 'OMP_NUM_THREADS', 'OMP_THREAD_LIMIT',
              'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS', 'BLIS_NUM_THREADS',
              'VECLIB_MAXIMUM_THREADS', 'NUMEXPR_NUM_THREADS', 'RAYON_NUM_THREADS')


def cpu_topology(cpus):
    result = []
    for cpu in sorted(cpus):
        base = Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
        row = {'cpu': cpu}
        for name in ('physical_package_id', 'core_id', 'thread_siblings_list'):
            try:
                value = (base / name).read_text().strip()
                row[name] = value if name == 'thread_siblings_list' else int(value)
            except (OSError, ValueError):
                row[name] = None
        result.append(row)
    return result


def resolve_resources(threads=1, cpus=None):
    if type(threads) is not int or not 1 <= threads <= 64:
        raise Error('invalid_threads', 'threads must be an integer in 1..64, including main')
    available = set(os.sched_getaffinity(0))
    if not available:
        raise Error('unavailable_cpus')
    if cpus is None:
        # One sibling from each physical core first, then remaining logical CPUs.
        first, rest, seen = [], [], set()
        for row in cpu_topology(available):
            key = (row['physical_package_id'], row['core_id'])
            if None in key:
                key = ('unknown', row['cpu'])
            (rest if key in seen else first).append(row['cpu'])
            seen.add(key)
        cpus = (first + rest)[:threads]
    else:
        if not isinstance(cpus, (list, tuple, set, frozenset)):
            raise Error('invalid_cpus', 'cpus must be a nonempty collection of unique CPU IDs')
        if not cpus or any(type(cpu) is not int or cpu < 0 for cpu in cpus) or len(set(cpus)) != len(cpus):
            raise Error('invalid_cpus')
        if set(cpus) - available:
            raise Error('unavailable_cpus', str(sorted(set(cpus) - available)))
    result = {'threads': threads, 'cpus': sorted(cpus),
              'cpu_budget_cores': min(threads, len(cpus)),
              'cpu_topology': cpu_topology(cpus), 'policy': 'native-resources-v1'}
    result['resource_digest'] = digest(result)
    return result
