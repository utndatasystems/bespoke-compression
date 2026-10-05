"""Obtain the public inputs; prepare the two author/upstream-supplied corpora."""
import argparse
from pathlib import Path
import subprocess
import sys
from urllib.request import urlopen
import shutil
import tempfile

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=HERE.parent / 'work/data')
    parser.add_argument('--python-source', type=Path, required=True, help='Exact author-supplied 99,999,986-byte corpus')
    parser.add_argument('--yelp-source', type=Path, required=True, help='Official Yelp business JSON acquired under upstream terms')
    args = parser.parse_args(); out = args.out.resolve()
    if out.exists() and any(out.iterdir()): parser.error('Use a new output directory')
    out.mkdir(parents=True, exist_ok=True)
    def run(argv): subprocess.run([sys.executable,*map(str,argv)],check=True)
    run([HERE / 'fetch_dbtext.py','--out',out / 'dbtext'])
    for dataset, source in [('python',args.python_source),('yelp',args.yelp_source)]:
        run([HERE / 'prepare.py',dataset,'--source',source.resolve(),'--out',out / dataset])
    with tempfile.TemporaryDirectory(prefix='bespoke-input-') as temporary:
        scratch = Path(temporary)
        archive = scratch / 'OpenStack.tar.gz'
        with urlopen('https://zenodo.org/records/8196385/files/OpenStack.tar.gz?download=1',timeout=None) as src, archive.open('wb') as dst:
            shutil.copyfileobj(src,dst)
        run([HERE / 'prepare.py','openstack','--source',archive,'--out',out / 'openstack'])
        repo = scratch / 'sqlstorm'
        subprocess.run(['git','clone','--filter=blob:none','--no-checkout','https://github.com/SQL-Storm/SQLStorm.git',str(repo)],check=True)
        subprocess.run(['git','-C',str(repo),'sparse-checkout','set','v1.0/tpcds/queries'],check=True)
        subprocess.run(['git','-C',str(repo),'checkout','b3bb0b96794a6afe9bb8f3ff2b243562b779c40d'],check=True)
        run([HERE / 'prepare.py','sqlstorm-tpcds','--source',repo / 'v1.0/tpcds/queries','--out',out / 'sqlstorm-tpcds'])

if __name__ == '__main__': main()
