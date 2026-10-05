#!/usr/bin/env python3
"""Validate sanitized usage and regenerate the current GPT accounting tables."""
import argparse
import csv
import hashlib
import io
import json
from collections import Counter
from datetime import datetime
from decimal import Decimal
from pathlib import Path

from pygments.lexers import CppLexer, GasLexer, PythonLexer
from pygments.token import Comment, Literal

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
TOKENS = ['input_tokens', 'cached_input_tokens', 'cache_write_input_tokens',
          'output_tokens', 'reasoning_output_tokens']
LEXERS = {'.c': CppLexer, '.cpp': CppLexer, '.h': CppLexer,
          '.hpp': CppLexer, '.py': PythonLexer, '.S': GasLexer}
# These files contain fitted tables or payload bytes, not implementation logic.
DATA_FILES = {
    ('dbtext-tools-allowed-A2', 'encode_plans.h'),
    ('python-from-scratch-A1', 'payload.inc'),
    ('python-from-scratch-A4', 'analysis_costs.h'),
    ('python-from-scratch-A4', 'analysis_costs_refit.h'),
    ('sqlstorm-tpcds-from-scratch-A4', 'final/lzfinal_payload.h'),
}


def code_lines(path):
    """Count physical lines containing code, retaining strings and directives."""
    text = path.read_text()
    lexer = LEXERS[path.suffix](stripnl=False, ensurenl=False)
    kept = []
    for kind, value in lexer.get_tokens(text):
        comment = (kind in Comment and kind not in Comment.Preproc
                   and kind not in Comment.PreprocFile) or kind in Literal.String.Doc
        kept.append(''.join('\n' if c == '\n' else ' ' for c in value)
                    if comment else value)
    return sum(bool(line.strip()) for line in ''.join(kept).splitlines())


def source_counts(key, directory, provenance):
    """Count each selected build file once, across encoder and decoder."""
    spec = json.loads((directory / 'build-spec.json').read_text())
    hashes = provenance.get('file_sha256', provenance.get('source_sha256'))
    assert hashes
    records = []
    for name in sorted(set(spec['sources'])):
        path = directory / 'source' / name
        digest = sha(path)
        assert digest == hashes['source/' + name], path
        if Path(name).parts[0] in ('ppmd', 'fsst'):
            exclusion = 'Imported third-party codec'
        elif (key, name) in DATA_FILES:
            exclusion = 'Generated data or fitted table'
        elif Path(name).parts[0] == 'interface':
            exclusion = 'Supplied lab interface'
        elif path.suffix not in LEXERS:
            exclusion = 'Documentation, metadata or non-code artifact'
        else:
            exclusion = ''
        records.append({'run_id': key, 'file': str(path.relative_to(REPO)),
                        'sha256': digest, 'code_lines': code_lines(path) if not exclusion else '',
                        'exclusion': exclusion})
    assert any(r['code_lines'] for r in records), key
    return records


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def timestamp(value):
    return datetime.fromisoformat(value.replace('Z', '+00:00')).timestamp()


def csv_text(rows):
    output = io.StringIO(newline='')
    writer = csv.DictWriter(output, fieldnames=list(rows[0]), lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def money(nano):
    return f'{Decimal(nano) / Decimal(10**9):.9f}'


def duration(seconds):
    minutes = int(float(seconds) / 60 + 0.5)
    return f'{minutes // 60}:{minutes % 60:02d}'


def build():
    source = json.loads((ROOT / 'sources.json').read_text())
    pricing = json.loads((ROOT / 'pricing.json').read_text())
    assert sha(ROOT / 'requests.csv') == source['requests_sha256']
    requests = list(csv.DictReader((ROOT / 'requests.csv').open()))
    assert len({r['request_id'] for r in requests}) == len(requests)
    responses = [r['response_id'] for r in requests if r['response_id']]
    assert len(responses) == len(set(responses))
    assert len(source['runs']) == 26
    assert Counter(s['funding'] for s in source['runs'].values()) == {
        'API': 21, 'ChatGPT subscription': 5}
    assert {r['run_id'] for r in requests} == set(source['runs'])
    assert all(s['model'] == 'gpt-6-astra' and s['effort'] == 'ultra'
               for s in source['sessions'].values())

    for r in requests:
        assert r['session_id'] in source['sessions']
        if r['status'] == 'uncertain':
            assert r['funding'] == 'API'
            assert all(r[k] == '' for k in TOKENS + ['recorded_cost_nanousd'])
            assert int(r['uncertain_reservation_nanousd']) > 0
            continue
        assert r['status'] in ('settled', 'reported_usage')
        n, c, w, o, reasoning = [int(r[k]) for k in TOKENS]
        assert min(n, c, w, o, reasoning) >= 0 and c + w <= n and reasoning <= o
        if r['funding'] == 'API':
            rates = pricing['long_context'] if n > pricing['long_context_threshold_input_tokens'] else pricing
            cost = ((n-c-w)*Decimal(rates['input']) + c*Decimal(rates['cached_input'])
                    + w*Decimal(rates['cache_write_input']) + o*Decimal(rates['output'])) * 1000
            assert cost == int(r['recorded_cost_nanousd']), r['request_id']
        else:
            assert not r['recorded_cost_nanousd'] and not r['uncertain_reservation_nanousd']

    for name, ledger in source['ledgers'].items():
        selected = [r for r in requests if r['request_id'].startswith(name + ':')]
        assert len(selected) == ledger['requests']
        assert sum(int(r['recorded_cost_nanousd'] or 0) for r in selected) == ledger['settled_cost_nanousd']

    runs = []
    files = []
    for key, meta in source['runs'].items():
        provenance = REPO / meta['published_provenance']
        assert sha(provenance) == meta['published_provenance_sha256']
        published = json.loads(provenance.read_text())
        assert published['result_id'] == meta['result_id']
        counted = source_counts(key, provenance.parent, published)
        files.extend(counted)
        selected = [r for r in requests if r['run_id'] == key]
        assert {r['session_id'] for r in selected} == set(meta['sessions'])
        if meta['funding'] != 'API':
            for rollout in meta['native_rollouts']:
                events = [r for r in selected if r['session_id'] == rollout['session_id']]
                assert len(events) == rollout['usage_events']
                assert all(sum(int(r[k]) for r in events) == rollout['final_total_token_usage'][k]
                           for k in TOKENS)
        start = min(r['started_utc'] for r in selected) if meta['funding'] == 'API' else meta['observed_start_utc']
        end = max(r['finished_utc'] for r in selected)
        cost = sum(int(r['recorded_cost_nanousd'] or 0) for r in selected)
        reservation = sum(int(r['uncertain_reservation_nanousd'] or 0) for r in selected)
        runs.append({'run_id':key, 'dataset':meta['dataset'], 'policy':meta['policy'],
            'variant':f'A{meta["stage"]}', 'funding':meta['funding'],
            'code_lines':sum(r['code_lines'] for r in counted if r['code_lines'] != ''),
            'requests_with_usage':sum(r['status'] != 'uncertain' for r in selected),
            'requests_without_usage':sum(r['status'] == 'uncertain' for r in selected),
            **{k:sum(int(r[k] or 0) for r in selected) for k in TOKENS},
            'started_utc':start, 'finished_utc':end,
            'elapsed_seconds':f'{timestamp(end)-timestamp(start):.6f}',
            'estimated_API_USD':money(cost) if meta['funding'] == 'API' else '',
            'uncertain_reservation_USD':money(reservation) if meta['funding'] == 'API' else '',
            'result_id':meta['result_id'], 'notes':' '.join(meta['notes'])})

    groups = []
    for key in dict.fromkeys((r['dataset'], r['policy']) for r in runs):
        selected = [r for r in runs if (r['dataset'], r['policy']) == key]
        paid = selected[0]['funding'] == 'API'
        groups.append({'dataset':key[0], 'policy':key[1], 'variants':len(selected),
            'funding':selected[0]['funding'],
            **{k:sum(r[k] for r in selected) for k in TOKENS},
            'estimated_API_USD':str(sum(Decimal(r['estimated_API_USD']) for r in selected)) if paid else '',
            'uncertain_reservation_USD':str(sum(Decimal(r['uncertain_reservation_USD']) for r in selected)) if paid else ''})
    total = sum(Decimal(r['estimated_API_USD']) for r in runs if r['estimated_API_USD'])
    unresolved = sum(Decimal(r['uncertain_reservation_USD']) for r in runs if r['uncertain_reservation_USD'])

    def cells(row, compact=False):
        return [row['dataset'], ('Scratch' if row['policy']=='from-scratch' else 'Tools'), row['variant'],
                f'{row["code_lines"]:,}',
                *[f'{row[k]/1e6:.3f}' for k in ['input_tokens','cached_input_tokens','output_tokens']],
                duration(row['elapsed_seconds']),
                f'${Decimal(row["estimated_API_USD"]):.2f}' if row['estimated_API_USD'] else ('--' if compact else 'Subscription')]

    header = ['Dataset','Setting','Variant','Code lines','Input (M)','Cached (M)','Output (M)','Elapsed (h:mm)','API estimate (USD)']
    table = ['| '+' | '.join(header)+' |', '|---|---|---|---:|---:|---:|---:|---:|---:|']
    table += ['| '+' | '.join(cells(r))+' |' for r in runs]
    summary = ['| Dataset | Setting | Variants | Recorded API estimate |', '|---|---|---:|---:|']
    summary += [f'| {g["dataset"]} | {g["policy"]} | {g["variants"]} | '+
                (f'${Decimal(g["estimated_API_USD"]):.2f}' if g['estimated_API_USD'] else 'ChatGPT subscription')+' |' for g in groups]

    tex = [r'\begingroup', r'\fontsize{8.5}{10.5}\selectfont', r'\setlength{\tabcolsep}{3pt}',
           r'\renewcommand{\arraystretch}{1.12}', r'\begin{tabular}{lllrrrrrr}', r'\toprule',
           r'Dataset & Setting & Variant & Code lines & Input & Cached & Output & Elapsed & API est. \\',
           r' & & & & [M] & [M] & [M] & [h:mm] & [USD] \\', r'\midrule']
    previous=None
    for row in runs:
        group=(row['dataset'],row['policy'])
        if previous is not None and previous!=group:tex.append(r'\midrule')
        values=cells(row,compact=True)
        if previous==group:values[:2]=['','']
        values[-1]=values[-1].replace('$',r'\$')
        tex.append(' & '.join(values)+r' \\')
        previous=group
    tex += [r'\midrule', rf'\multicolumn{{8}}{{r}}{{Total recorded API estimate}} & \${total:.2f} \\',
            r'\bottomrule', r'\end{tabular}', r'\endgroup', '']

    readme = (ROOT/'README.md').read_text()
    for name, content in [('SUMMARY', '\n'.join(summary)), ('TABLE', '\n'.join(table))]:
        a, rest=readme.split(f'<!-- BEGIN {name} -->')
        _, b=rest.split(f'<!-- END {name} -->')
        readme=a+f'<!-- BEGIN {name} -->\n'+content+f'\n<!-- END {name} -->'+b
    generated = {'runs.csv':csv_text(runs), 'summary.csv':csv_text(groups),
                 'code-lines.csv':csv_text(files),
                 'table.tex':'\n'.join(tex), 'README.md':readme}
    print(f'Validated {len(runs)} variants, {len(requests)} usage/request records; '
          f'API estimate ${total:.9f}; separate unresolved reservation ${unresolved:.9f}.')
    return generated


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args=parser.parse_args()
    for name, content in build().items():
        path=ROOT/name
        if args.write:path.write_text(content)
        else:assert path.read_text()==content, f'{name} differs; use --write to regenerate'
