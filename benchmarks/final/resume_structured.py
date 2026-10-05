"""Explicitly continue a stopped structured replay without repeating successes.

Only named failed methods may be retried, once. Original result/evidence files,
sealed limits, native operation timers and trial counts remain unchanged.
"""
import argparse
import csv
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'compression-lab-isolated/vendor'))
from compression_lab import strings, runner
from compression_lab.util import digest, save, sha

def module(name, path):
    spec=importlib.util.spec_from_file_location(name,path)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value)
    return value

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dataset',choices=['openstack','python','yelp','sqlstorm-tpcds'],required=True)
    p.add_argument('--data',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--cpu',type=int,required=True)
    p.add_argument('--project-lock',type=Path,required=True)
    p.add_argument('--retry-failed',nargs='*',default=[])
    p.add_argument('--inspect',action='store_true',help='Verify receipts and print the plan without measuring')
    a=p.parse_args();work=a.work.resolve();data=a.data.resolve()
    final=module('final_replay',ROOT/'benchmarks/final/run.py')
    native=module('native_replay',ROOT/'benchmarks/dbtext/run.py')
    runner.HOST_LOCK=Path(os.environ.get('COMPRESSION_BENCHMARK_LOCK','/tmp/compression-lab-host-benchmark-v1.lock'))
    folder=work/'final-benchmarks'/a.dataset;out=folder/'native'
    assert folder.is_dir() and not (folder/'COMPLETE.json').exists()
    assert not (out/'summary.json').exists() and not (out/'paper-results.csv').exists()
    c=strings.config(out)
    assert c['cpu']==a.cpu and c['trials']==7 and c['warmups']==1
    assert c['memory_bytes']==2*1024**3
    pins=json.loads((ROOT/'datasets'/(a.dataset+'.json')).read_bytes())
    assert [(x['name'],x['bytes'],x['sha256']) for x in c['columns']]==[(x['name'],x['bytes'],x['sha256']) for x in pins]
    assert all(Path(x['path'])==data/a.dataset/x['name'] for x in c['columns'])
    build=work/'final-benchmarks'/(a.dataset+'-baselines')
    profile=json.loads((build/'build-profile.json').read_bytes())
    selected=[x for x in final.entries() if x['dataset']==a.dataset]
    assert len(selected)==4
    manifests=[build/name/'manifest.json' for name in profile['methods']]
    manifests += [work/'build'/x['id']/'manifest.json' for x in selected]
    entries=native.prepare_methods(manifests,'bulk',profile['row_framing'])
    assert len(entries)==25 and len(set(a.retry_failed))==len(a.retry_failed)
    records={}
    for path in sorted((out/'results').glob('*.json')):
        row=json.loads(path.read_bytes());rid=row['result_id']
        assert rid=='s-'+digest({k:v for k,v in row.items() if k!='result_id'}),path
        assert row['workload_digest']==digest(c) and row['depth']=='full'
        assert sha(Path(row['evidence'])/'trials.jsonl')==row['raw_trials_sha256']
        records.setdefault(row['name'],[]).append(row)
    assert set(records)<=set(x['name'] for x in entries)
    retained={};failed=[];missing=[]
    for entry in entries:
        rows=records.get(entry['name'],[])
        for row in rows:
            assert row['candidate']=={role:sha(entry[role]) for role in ('encoder','decoder')}
        good=[row for row in rows if row['quality_passed'] and row['status']=='measured']
        assert len(good)<=1,'Multiple successful receipts require review: '+entry['name']
        if good:
            assert good[0]['roundtrips']==8*len(pins)
            strings.compare(out,[good[0]['result_id']])
            retained[entry['name']]=good[0]
        elif rows:
            assert len(rows)==1 and rows[0]['status']=='failed','Repeated failures require review'
            failed.append(entry['name'])
        else:
            missing.append(entry['name'])
    assert set(a.retry_failed)<=set(failed),'Only failed methods may be explicitly retried'
    assert set(a.retry_failed)==set(failed),'Every unresolved failure needs an explicit retry choice'
    order=missing+[x for x in failed if x in a.retry_failed]
    plan={'dataset':a.dataset,'unchanged_successes':{k:v['result_id'] for k,v in retained.items()},
          'failed_receipts':{k:[v['result_id'] for v in records[k]] for k in failed},
          'measurement_order':order,'timeout_seconds':None,'sealed_timeout_seconds':c.get('timeout_seconds'),'trials':7,'warmups':1,
          'cpu':a.cpu,'config_sha256':sha(out/'strings.json'),
          'sources':{str(path.relative_to(ROOT)):sha(path) for path in
                    [Path(__file__),ROOT/'benchmarks/final/run.py',ROOT/'benchmarks/dbtext/run.py',
                     ROOT/'compression-lab-isolated/vendor/compression_lab/strings.py']}}
    print(json.dumps(plan,indent=2),flush=True)
    if a.inspect:return
    request=folder/'RESUME_REQUEST.json'
    assert not request.exists(),'An existing continuation must be inspected, not duplicated'
    save(request,plan)
    with a.project_lock.open('a') as lease:
        fcntl.flock(lease,fcntl.LOCK_EX)
        by_name={x['name']:x for x in entries}
        for name in order:
            entry=by_name[name];target=out/'candidates'/name
            if target.exists():
                assert name in failed
                assert all(sha(target/(role+'.so'))==sha(entry[role]) for role in ('encoder','decoder'))
            else:
                target.mkdir(parents=True)
                for role in ('encoder','decoder'):shutil.copyfile(entry[role],target/(role+'.so'))
                save(target/'manifest.json',dict(name=name,variant=entry['variant'],encoder='encoder.so',decoder='decoder.so'))
                save(target/'provenance.json',dict(source_manifest=str(entry['manifest']),source_sha256=sha(entry['manifest']),
                     capability=entry['capability'],evaluation_variant=entry['variant'],
                     binaries={role:sha(target/(role+'.so')) for role in ('encoder','decoder')}))
            print('Measuring previously unmeasured' if name in missing else 'Explicit unchanged-limit retry',name,flush=True)
            row=strings.evaluate(out,target/'manifest.json',run=native.native_run,wait_for_lease=True)
            if not row['quality_passed']:
                save(folder/'RESUME_OUTCOME.json',{'status':'failed','result_id':row['result_id'],
                     'error':row.get('error'),'completed':list(retained),'request_sha256':sha(request)})
                raise RuntimeError(row.get('error'))
            retained[name]=row
        results=[retained[x['name']] for x in entries]
        strings.compare(out,[x['result_id'] for x in results])
        save(out/'summary.json',{'smoke':False,'results':results})
        csv_rows=[]
        for row in results:
            accounting=row['accounting'];n=row['original_bytes']
            custom=0 if row['name'] in profile['methods'] else accounting['custom_decoder_bytes']
            assert accounting['nonplatform_dependency_bytes']==accounting['excluded_standard_codec_bytes']
            package=accounting['archive_bytes']+custom
            csv_rows.append(dict(method=row['name'],result_id=row['result_id'],original_bytes=n,
                 archive_bytes=accounting['archive_bytes'],codec_bytes=custom,package_bytes=package,
                 package_factor=n/package,bits_per_byte=8*package/n,roundtrip_failures=0,
                 encode_MB_s=row['timing']['encode_seconds']['work_per_second']/1e6,
                 decode_MB_s=row['timing']['decode_seconds']['work_per_second']/1e6,
                 strict_deployment_bytes=accounting['strict_deployment_bytes']))
        final.csvwrite(out/'paper-results.csv',csv_rows)
        extra=[]
        with runner.HOST_LOCK.open('a') as host:
            fcntl.flock(host,fcntl.LOCK_EX)
            for name,kind in [('FSST-column','fsst'),('OnPair+','onpair')]:
                print(a.dataset,name,flush=True)
                extra.append(final.measure_original(name,data/a.dataset,pins,folder/name,
                             work/'final-benchmarks/drivers'/('bulk-'+kind),a.cpu))
        final.update_bulk(work/'fresh-figure-inputs',a.dataset,out/'paper-results.csv',selected,extra)
        save(folder/'COMPLETE.json',{'dataset':a.dataset,'full':True,'byte_exact':True})
        save(folder/'RESUME_OUTCOME.json',{'status':'passed','request_sha256':sha(request),
             'retained_original_successes':len(plan['unchanged_successes']),
             'unmeasured_configurations':len(missing),'explicit_failed_retries':len(failed)})
        print(a.dataset,'complete with failed original receipt retained',flush=True)

if __name__=='__main__':main()
