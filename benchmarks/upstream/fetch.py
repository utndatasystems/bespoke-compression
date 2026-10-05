"""Fetch the exact OnPair+ dependency snapshot used by the benchmarks."""
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parent


def main():
    lock = json.loads((ROOT / 'onpair-lock.json').read_text())
    base = lock['repository'].replace('github.com', 'raw.githubusercontent.com')
    for entry in lock['files']:
        path = ROOT / 'token' / entry['path']
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']:
            continue
        with urlopen(f"{base}/{lock['commit']}/{entry['path']}", timeout=None) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise RuntimeError(f"Upstream checksum mismatch: {entry['path']}")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"Verified {len(lock['files'])} upstream files at {lock['commit']}")


if __name__ == '__main__':
    main()
