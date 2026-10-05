"""Extract the bundled ZIPs and verify every exact paper input."""
import argparse
import json
from pathlib import Path
import shutil
import stat
import zipfile

from prepare import check, digest

HERE = Path(__file__).resolve().parent

def unpack(receipt, archives, out):
    dataset = receipt['dataset']
    archive_path = archives / Path(receipt['path']).name
    if not archive_path.is_file():
        raise ValueError(f'Missing bundled dataset: {archive_path}')
    if archive_path.stat().st_size != receipt['bytes'] or digest(archive_path) != receipt['sha256']:
        raise ValueError(f'Archive identity mismatch: {archive_path}')
    manifest = json.loads((HERE / receipt['manifest']).read_text(encoding='utf-8'))
    expected = {dataset + '/' + item['name']: item for item in manifest}
    if len(expected) != receipt['files']:
        raise ValueError('Archive manifest count mismatch: ' + dataset)
    with zipfile.ZipFile(archive_path) as archive:
        members = archive.infolist()
        if len(members) != len(expected) or {info.filename for info in members} != set(expected):
            raise ValueError('Unexpected or duplicate ZIP members: ' + dataset)
        for info in members:
            item = expected[info.filename]
            if (Path(item['name']).name != item['name'] or info.is_dir()
                    or stat.S_ISLNK(info.external_attr >> 16) or info.file_size != item['bytes']):
                raise ValueError('Invalid ZIP member: ' + info.filename)
        folder = out / dataset
        folder.mkdir()
        for info in members:
            # Open only the explicitly pinned basename, never extract arbitrary paths.
            target = folder / expected[info.filename]['name']
            with archive.open(info) as source, target.open('xb') as destination:
                shutil.copyfileobj(source, destination, length=1024 * 1024)
    columns = check(dataset, folder)
    print(f"Verified {len(columns)} files, {sum(item['bytes'] for item in columns):,} bytes: {dataset}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=HERE.parent / 'work/data')
    parser.add_argument('--archives', type=Path, default=HERE / 'archives', help='Directory containing the five pinned ZIPs')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('Use a new or empty output directory; partial attempts are retained for inspection')
    out.mkdir(parents=True, exist_ok=True)
    for receipt in json.loads((HERE / 'ARCHIVES.json').read_text(encoding='utf-8')):
        unpack(receipt, args.archives.resolve(), out)

if __name__ == '__main__': main()
