"""Explicit owner-sealed accounting. No baseline names or reference tables."""
import math
from compression_lab.util import Error, digest, save, sha


def seal(lab, *, target, hardware, remote=None, standard_libraries=None, dbtext_target=None):
    if lab.jobs()['jobs'] or (lab.owner/'FINISH.json').exists():
        raise Error('seal_before_research')
    if set(target) != {'maximum_package_bytes','minimum_decode_MB_s'}:
        raise Error('invalid_target')
    if type(target['maximum_package_bytes']) is not int or target['maximum_package_bytes'] <= 0 or not math.isfinite(target['minimum_decode_MB_s']) or target['minimum_decode_MB_s'] <= 0:
        raise Error('invalid_target')
    if dbtext_target is not None:
        from .dbtext import PERCENTS, PROTOCOL, prepare
        if set(dbtext_target) != {'maximum_package_bytes','minimum_rows_M_s'} or \
            type(dbtext_target['maximum_package_bytes']) is not int or dbtext_target['maximum_package_bytes']<=0 or \
            set(dbtext_target['minimum_rows_M_s']) != set(PERCENTS) or any(
                not math.isfinite(v) or v<=0 for v in dbtext_target['minimum_rows_M_s'].values()):
            raise Error('invalid_dbtext_target')
        prepare(lab)
    protocol=lab.config['protocol']
    from .api import interface_pins
    lab.config['interface_pins']=interface_pins()
    protocol['interface_runtime_id']=digest(lab.config['interface_pins'])
    protocol['measurement_status']='sealed_single_core_bulk_trial'
    protocol['scoring']={'status':'sealed','primary_score':'package_factor_and_fresh_decode_MB_s',
        'package':'all archives plus complete custom decoder binary plus non-exempt decoder dependencies',
        'excluded':'encoder and source files; platform C/C++ runtime and the exact pinned standard libraries listed here',
        'standard_libraries':standard_libraries or [],
        'target':target,'encoding_floor_MB_s':None}
    if dbtext_target is not None:
        protocol['measurement_status']='sealed_joint_dbtext'
        protocol['scoring'].update(primary_score='same_package_bulk_and_all_row_targets',dbtext_target=dbtext_target)
        protocol['dbtext']=PROTOCOL
        protocol['evaluation_tool']='evaluate_dbtext'
        protocol['candidate_manifest']['variants']=['rows']
        protocol['measurement']={
            'bulk':{'warmups':1,'trials':7,'quick_trials':1,
                    'timer':'fresh decoder setup plus complete byte reconstruction',
                    'aggregation':'sum column times in each trial, then median and observed min/max'},
            'rows':PROTOCOL,
            'encode_scope':'lab_encode call only; separately report external fitting, preprocessing and required data-dependent compilation',
            'size_scope':protocol['scoring']['package']}
    protocol['hardware']=hardware
    protocol['measurement']['size_scope']=protocol['scoring']['package']
    protocol['candidate_manifest']['source_build']={
        'required':['name','variant','sources','commands','encoder','decoder'],
        'commands':'argv arrays; immutable /source with /source/interface/codec.h, writable /output; no build network',
        'reproducibility':'two clean builds must produce identical binary bytes'}
    if remote:
        lab.config['remote']=remote
        protocol['native_adapter']['cpu']=remote['cpu']
        protocol['measurement']['location']='benchmark server; research commands and builds run on the client'
        if lab.config.get('server_local'):
            protocol['measurement']['location']='benchmark server; research and builds on CPUs 12-19, official measurement CPU 8'
    protocol['protocol_id']=digest({k:v for k,v in protocol.items() if k!='protocol_id'})
    save(lab.public/'interface/protocol.json',protocol,0o444)
    lab.config['public_pins']['interface/protocol.json']=sha(lab.public/'interface/protocol.json')
    save(lab.owner/'config.json',lab.config,0o400)


def score(row, policy):
    result=dict(policy)
    sizes=row['sizes']
    if not row['correctness']['byte_exact'] or any(sizes[k] is None for k in ('archive_bytes','custom_decoder_bytes','nonplatform_dependency_bytes')):
        return {**result,'target_met':False,'reason':'byte_exact_measurement_required'}
    strict_total=sum(sizes[k] for k in ('archive_bytes','custom_decoder_bytes','nonplatform_dependency_bytes'))
    pins={(d['soname'],d['sha256'],d['bytes']) for d in policy.get('standard_libraries',[])}
    exempt=sum(d['bytes'] for d in row['dependencies'] if not d['platform'] and
               (d['soname'],d['sha256'],d['bytes']) in pins)
    total=strict_total-exempt
    timing=row['measurements']['bulk']['decode_seconds']
    speed=timing['work_per_second']/1e6
    result.update(package_bytes=total,strict_package_bytes=strict_total,exempt_standard_library_bytes=exempt,
                  compression_factor=row['original_bytes']/total,
                  decode_MB_s=speed,decode_low_MB_s=row['original_bytes']/timing['max_seconds']/1e6,
                  decode_high_MB_s=row['original_bytes']/timing['min_seconds']/1e6,
                  target_met=row['depth']=='full' and total<=policy['target']['maximum_package_bytes'] and speed>policy['target']['minimum_decode_MB_s'],
                  scope='Measured target comparison; final source and timing-spread review remains separate')
    if 'dbtext_target' in policy:
        target=policy['dbtext_target']
        rows=row['measurements'].get('dbtext',{})
        valid=rows.get('byte_exact') is True and rows.get('same_configuration') is True and rows.get('replays')==3
        comparisons={p: bool(valid and p in rows.get('selective',{}) and
            rows['selective'][p]['median_rows_M_s']>speed) for p,speed in target['minimum_rows_M_s'].items()}
        row_pass=valid and total<=target['maximum_package_bytes'] and all(comparisons.values())
        result.update(bulk_target_met=result['target_met'],rows_target_met=bool(row_pass),
                      row_speed_targets_met=comparisons,target_met=bool(result['target_met'] and row_pass))
    return result
