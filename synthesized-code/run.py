#!/usr/bin/env python3
"""Check exact corpus reconstruction; row-capable codecs also check selected rows."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path

COLUMNS = ('c_name chinese city credentials email faust firstname genome hamlet hex '
           'japanese l_comment lastname location movies ps_comment street urls urls2 '
           'uuid wiki wikipedia yago').split()


def check(run, path, build_dir=None):
    spec = json.loads((run / 'build-spec.json').read_text())
    build_dir = build_dir or run / 'build'
    encoder = C.CDLL(str(build_dir / spec['encoder']))
    decoder = C.CDLL(str(build_dir / spec['decoder']))
    encoder.lab_encode.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
    encoder.lab_encode.restype = C.c_int64
    decoder.lab_open.argtypes = [C.c_void_p, C.c_size_t]
    decoder.lab_open.restype = C.c_void_p
    decoder.lab_decode.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    decoder.lab_decode.restype = C.c_int64
    if spec['variant'] == 'rows':
        decoder.lab_rows.argtypes = [C.c_void_p, C.POINTER(C.c_uint64), C.c_size_t,
                                    C.c_void_p, C.c_size_t, C.POINTER(C.c_uint64)]
        decoder.lab_rows.restype = C.c_int64
    decoder.lab_close.argtypes = [C.c_void_p]
    decoder.lab_close.restype = None
    raw = path.read_bytes()
    source = C.create_string_buffer(raw)
    archive = C.create_string_buffer(max(1024 * 1024, 3 * len(raw)))
    size = encoder.lab_encode(source, len(raw), archive, len(archive))
    if not 0 <= size <= len(archive):
        raise RuntimeError(f'{path.name}: encoding failed ({size})')
    state = decoder.lab_open(archive, size)
    if not state:
        raise RuntimeError(f'{path.name}: opening archive failed')
    try:
        output = C.create_string_buffer(max(1, len(raw)))
        count = decoder.lab_decode(state, output, len(raw))
        if count != len(raw) or output.raw[:count] != raw:
            raise RuntimeError(f'{path.name}: bulk reconstruction differs')
        ends = [i + 1 for i, b in enumerate(raw) if b == 10] if spec['variant'] == 'rows' else []
        if raw and (not ends or ends[-1] != len(raw)):
            ends.append(len(raw))
        offsets = [0] + ends
        selected = sorted({0, len(ends) // 2, len(ends) - 1}) if ends else []
        for ids in ((selected, list(range(len(ends)))) if spec['variant'] == 'rows' else ()):
            indices = (C.c_uint64 * len(ids))(*ids)
            boundaries = (C.c_uint64 * (len(ids) + 1))()
            count = decoder.lab_rows(state, indices, len(ids), output, len(raw), boundaries)
            expected = b''.join(raw[offsets[i]:offsets[i + 1]] for i in ids)
            expected_offsets = [0]
            for i in ids:
                expected_offsets.append(expected_offsets[-1] + offsets[i + 1] - offsets[i])
            if (count != len(expected) or output.raw[:count] != expected
                    or list(boundaries) != expected_offsets):
                raise RuntimeError(f'{path.name}: selected-row reconstruction differs')
    finally:
        decoder.lab_close(state)
    print(f'PASS {path.name}: bulk' + (' and selected rows' if spec['variant'] == 'rows' else ''), flush=True)
    return {'input': path.name, 'bytes': len(raw), 'input_sha256': hashlib.sha256(raw).hexdigest(),
            'archive_bytes': size, 'archive_sha256': hashlib.sha256(archive.raw[:size]).hexdigest(),
            'bulk_roundtrip': True, 'selected_rows_roundtrip': True if spec['variant'] == 'rows' else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, help='Codec folder previously built with build.py')
    parser.add_argument('input', type=Path, help='One original column or directory of all 23 columns')
    parser.add_argument('--build-dir', type=Path, help='Output directory passed to build.py')
    parser.add_argument('--dataset', choices=['openstack', 'python', 'yelp', 'sqlstorm-tpcds', 'dbtext'])
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    run = args.run.resolve()
    dataset = args.dataset or next((s for s in run.parts if s in {'openstack', 'python', 'yelp', 'sqlstorm-tpcds', 'dbtext'}), None)
    if dataset is None:
        parser.error('Specify --dataset')
    root = Path(__file__).resolve().parents[1]
    identities = json.loads((root / 'datasets' / (dataset + '.json')).read_bytes())
    paths = [args.input / p['name'] for p in identities] if args.input.is_dir() else [args.input]
    for path in paths:
        if not path.is_file():
            parser.error(f'Missing input: {path}')
        expected = next((p for p in identities if p['name'] == path.name), None)
        if expected is None or path.stat().st_size != expected['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != expected['sha256']:
            parser.error('Input identity mismatch: ' + str(path))
    records = []
    for path in paths:
        records.append(check(run, path, args.build_dir.resolve() if args.build_dir else None))
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps({'dataset': dataset, 'files': records}, indent=2) + '\n')
    print(f'Verified {len(paths)} complete input file(s).')


if __name__ == '__main__':
    main()
