"""Prepare locally supplied source data and verify the exact paper inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

HERE=Path(__file__).resolve().parent


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while data:=f.read(1024*1024):h.update(data)
    return h.hexdigest()


def check(dataset, folder):
    columns=json.loads((HERE/(dataset+'.json')).read_text())
    for col in columns:
        p=folder/col['name']
        if not p.is_file() or p.stat().st_size!=col['bytes'] or digest(p)!=col['sha256']:
            raise ValueError('Input identity mismatch: '+str(p))
    return columns


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dataset',choices=['dbtext','python','yelp','openstack','sqlstorm-tpcds'])
    ap.add_argument('--source',type=Path,help='Extracted source file/directory, or the OpenStack tar.gz')
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--check',action='store_true',help='Check an existing prepared directory without modifying it')
    args=ap.parse_args()
    if args.check:
        columns=check(args.dataset,args.out)
    else:
        if args.source is None:ap.error('--source is required for preparation')
        args.out.mkdir(parents=True,exist_ok=False)
        columns=json.loads((HERE/(args.dataset+'.json')).read_text())
        if args.dataset=='sqlstorm-tpcds':
            files=sorted(args.source.rglob('*.sql'),key=lambda p:p.relative_to(args.source).as_posix())
            if len(files)!=15242:raise ValueError('Expected the 15,242 SQLStorm TPC-DS query files')
            with (args.out/'queries.nul').open('wb') as f:
                for path in files:f.write(path.read_bytes()+b'\0')
        elif args.dataset=='yelp':
            with args.source.open('rb') as source,(args.out/'corpus.bin').open('wb') as out:
                size=0
                for line in source:
                    out.write(line);size+=len(line)
                    if size>=100_000_000:break
        elif args.dataset=='python':
            shutil.copyfile(args.source,args.out/'corpus.bin')
        elif args.dataset=='openstack' and args.source.is_file():
            if digest(args.source)!='87c98c5ed03262e05cdb7a6f3717033df76d88fda0f7d2db23bd9fa4200f1879':
                raise ValueError('Wrong OpenStack source archive')
            with tarfile.open(args.source) as archive:
                for col in columns:
                    members=[m for m in archive.getmembers() if m.isfile() and Path(m.name).name==col['name']]
                    if len(members)!=1:raise ValueError('Missing or ambiguous archive member: '+col['name'])
                    with archive.extractfile(members[0]) as src,(args.out/col['name']).open('wb') as dst:shutil.copyfileobj(src,dst)
        else:
            for col in columns:shutil.copyfile(args.source/col['name'],args.out/col['name'])
        check(args.dataset,args.out)
    print(f"Verified {len(columns)} files, {sum(c['bytes'] for c in columns):,} original bytes: {args.dataset}")


if __name__=='__main__':main()
