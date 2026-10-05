"""Download the 23 original DBText columns and verify their recorded hashes."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
    for entry in json.loads((HERE/'DBTEXT_SOURCES.json').read_text())['files']:
        target = a.out / Path(entry['path']).name
        if target.exists():
            data = target.read_bytes()
        else:
            with urlopen(entry['url'],timeout=None) as response: data=response.read()
        if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:
            raise RuntimeError('Wrong column bytes: '+target.name)
        if not target.exists(): target.write_bytes(data)
    print('Verified all 23 original columns')


if __name__ == '__main__':
    main()
