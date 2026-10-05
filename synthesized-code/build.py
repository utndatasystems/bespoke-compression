#!/usr/bin/env python3
"""Build an immutable source snapshot with its recorded compiler flags."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

PROGRAMS = {'gcc', 'g++', 'clang', 'clang++', 'strip', 'ld', 'objcopy', 'python3'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(run):
    spec = json.loads((run / 'build-spec.json').read_bytes())
    provenance = json.loads((run / 'provenance.json').read_bytes())
    hashes = provenance.get('file_sha256', provenance.get('source_sha256', {}))
    if not hashes:
        raise ValueError('No retained source hashes: ' + str(run))
    for name, expected in hashes.items():
        path = (run / name).resolve()
        if not path.is_relative_to(run) or not path.is_file() or sha(path) != expected:
            raise ValueError('Retained file differs from its recorded hash: ' + name)
    return spec, hashes


def build(run, output, dry_run=False):
    run, output = run.resolve(), output.resolve()
    spec, hashes = verify(run)
    source = output / 'source'
    if not dry_run:
        output.mkdir(parents=True, exist_ok=False)
        shutil.copytree(run / 'source', source)
    relocations = []
    # Relocate logical include/incbin paths only in the build copy. Original
    # submitted source bytes and their hashes remain unchanged.
    for original in (run / 'source').rglob('*'):
        if original.suffix not in {'.S', '.c', '.cpp', '.h', '.hpp'}:
            continue
        raw = original.read_text()
        changed = raw.replace('"/source/', '"' + str(source) + '/')
        if changed != raw:
            relocations.append(original.relative_to(run).as_posix())
            if not dry_run:
                (source / original.relative_to(run / 'source')).write_text(changed)
    commands = []

    def execute(argv):
        commands.append(argv)
        print(shlex.join(argv), flush=True)
        if not dry_run:
            subprocess.run(argv, cwd=output, check=True, timeout=None)

    for command in spec['commands']:
        if command[0] not in PROGRAMS:
            raise ValueError('Unsupported retained build program: ' + command[0])
        paths = {'source': str(source), 'output': str(output)}
        argv = [re.sub(r'/(source|output)(?=/|$)', lambda m: paths[m[1]], arg)
                for arg in command]
        if command[0] == 'python3':
            argv[0] = sys.executable
        execute(argv)
        # ld embeds its input pathname in symbols. Restore the original /source
        # names so relocation does not change the encoder's fitted-data binding.
        if command[0] == 'ld' and '-b' in command and command[command.index('-b') + 1] == 'binary':
            old = '_binary_' + re.sub(r'[^A-Za-z0-9_]', '_', argv[-1])
            expected = '_binary_' + re.sub(r'[^A-Za-z0-9_]', '_', command[-1])
            obj = argv[argv.index('-o') + 1]
            execute(['objcopy', *['--redefine-sym=' + old + '_' + part + '=' + expected + '_' + part
                                 for part in ('start', 'end', 'size')], obj])
    if dry_run:
        return
    for role in ('encoder', 'decoder'):
        if not (output / spec[role]).is_file():
            raise FileNotFoundError(output / spec[role])
    name = run.relative_to(Path(__file__).resolve().parent).as_posix().replace('/', '-')
    manifest = {'name': name, 'variant': spec['variant'],
                'encoder': spec['encoder'], 'decoder': spec['decoder']}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    receipt = {'source_sha256': hashes, 'build_spec_sha256': sha(run / 'build-spec.json'),
               'recipe_status': spec.get('recipe_status', 'recorded_commands'),
               'commands': commands, 'source_path_relocations': relocations,
               'binary_sha256': {role: sha(output / spec[role]) for role in ('encoder', 'decoder')}}
    (output / 'build-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Built and recorded ' + str(output / 'manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    build(args.run, args.output or args.run / 'build', args.dry_run)


if __name__ == '__main__':
    main()
