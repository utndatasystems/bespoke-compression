"""Native workload choices, independent of how the input delimits records."""
from .util import Error

BASELINES = {
    'bulk': ('lz4', 'zstd1', 'zstd3', 'zstd19'),
    'query-access': ('fsst', 'onpairplus'),
}


def variants(workload, row_framing):
    if workload is None:
        return ['bulk'] if row_framing == 'none' else ['bulk', 'rows']
    if workload not in (*BASELINES, 'both'):
        raise Error('native_invalid_workload')
    if workload != 'bulk' and row_framing == 'none':
        raise Error('strings_rows_not_configured')
    return {'bulk': ['bulk'], 'query-access': ['rows'], 'both': ['bulk', 'rows']}[workload]
