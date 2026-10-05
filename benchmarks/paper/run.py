"""Replay one complete paper dataset and export consistently charged sizes."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dataset',choices=['dbtext','python','yelp','openstack','sqlstorm-tpcds'],required=True)
    ap.add_argument('--data-dir',type=Path,required=True)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--candidate',type=Path,action='append',default=[])
    ap.add_argument('--cpu',type=int,required=True)
    ap.add_argument('--deps-prefix',type=Path)
    args=ap.parse_args();build=args.build_dir.resolve();out=args.out.resolve()
    profile=json.loads((build/'build-profile.json').read_text())
    baselines=profile['methods'];manifests=[build/name/'manifest.json' for name in baselines]+args.candidate
    cmd=[sys.executable,str(ROOT/'benchmarks/dbtext/run.py'),'--data-dir',str(args.data_dir.resolve()),
         '--columns',str(ROOT/'datasets'/(args.dataset+'.json')),'--build-dir',str(build),'--out',str(out),
         '--row-framing',profile['row_framing'],'--workload','bulk','--methods','--cpu',str(args.cpu)]
    for manifest in manifests:cmd+=['--candidate',str(manifest.resolve())]
    if args.deps_prefix:cmd+=['--deps-prefix',str(args.deps_prefix.resolve())]
    subprocess.run(cmd,check=True)
    results=json.loads((out/'summary.json').read_text())['results'];rows=[]
    for result in results:
        a=result['accounting'];n=result['original_bytes']
        custom=0 if result['name'] in baselines else a['custom_decoder_bytes']
        if a['nonplatform_dependency_bytes']!=a['excluded_standard_codec_bytes']:raise RuntimeError('Uncharged nonstandard decoder dependency')
        package=a['archive_bytes']+custom
        rows.append(dict(method=result['name'],result_id=result['result_id'],original_bytes=n,archive_bytes=a['archive_bytes'],
            codec_bytes=custom,package_bytes=package,package_factor=n/package,bits_per_byte=8*package/n,
            roundtrip_failures=0,
            encode_MB_s=result['timing']['encode_seconds']['work_per_second']/1e6,
            decode_MB_s=result['timing']['decode_seconds']['work_per_second']/1e6,
            strict_deployment_bytes=a['strict_deployment_bytes']))
    with (out/'paper-results.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
    print(out/'paper-results.csv')


if __name__=='__main__':main()
