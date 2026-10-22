"""Freeze/verify an M5 test image and software evidence. Never access hardware.

The manifest digest must be supplied independently by the physical approval record.
The manifest and compiler/header originals are private evidence: they contain paths.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

from m3_errno import verify_errno_map
from m3_image import require
from m5_image import check_image
from prepare_m3_errno import prepare as prepare_errno

_SPEC = importlib.util.spec_from_file_location(
    'm5_build_provenance', Path(__file__).resolve().parents[2] / 'scripts/record-build-provenance.py')
_PROVENANCE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_PROVENANCE)
source_snapshot = _PROVENANCE.source_snapshot
PINNED = {
    'zephyr': '684c9e8f32e4373a21098559f748f06915f950c9',
    'cmsis': '512cc7e895e8491696b61f7ba8066b4a182569b8',
    'cmsis_6': '30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74',
    'hal_nxp': '2c2f28ac333e995d7279777409817c4b4f92c1ec',
}
ROLES = {'binary', 'elf', 'config', 'map', 'dts', 'compilation', 'inputs', 'provenance', 'errno'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def frozen_inputs(data):
    source = data['source']
    require(not source['dirty'] and source['recoverable_from_git'], 'Unfrozen build inputs')
    require(re.fullmatch(r'[0-9a-f]{40}', source['git_commit']) is not None and
            re.fullmatch(r'[0-9a-f]{64}', source['snapshot_sha256']) is not None,
            'Incomplete source identity')
    projects = data['west_projects']
    require(len(projects) == len(PINNED) and {p['name'] for p in projects} == set(PINNED),
            'Unexpected/missing/duplicate dependencies')
    for project in projects:
        require(project['revision'] == project['git_commit'] == PINNED[project['name']] and
                not project['dirty'], 'Dependency changed')


def default_tools(repo):
    # Freeze executable physical tooling separately from the complete source snapshot.
    paths = [p for p in (repo / 'tests/target').glob('*.py') if p.is_file()]
    paths.extend((repo / 'scripts' / name) for name in (
        'test-networking-physical.sh', 'record-build-provenance.py'))
    return sorted(paths)


def prepare(build, inputs, output, repo, *, tool_paths=None):
    build, inputs, output, repo = map(Path, (build, inputs, output, repo))
    if output.exists():
        raise FileExistsError(output)
    data = json.loads(inputs.read_text())
    frozen_inputs(data)
    require(source_snapshot(repo) == data['source'], 'Source changed since input capture')
    folder = build / 'zephyr'
    files = dict(binary=folder / 'zephyr.bin', elf=folder / 'zephyr.elf',
                 config=folder / '.config', map=folder / 'zephyr.map',
                 dts=folder / 'zephyr.dts', compilation=build / 'compile_commands.json', inputs=inputs,
                 provenance=build / 'provenance.json')
    provenance = json.loads(files['provenance'].read_text())
    require(provenance['source'] == data['source'] and
            provenance['west_projects'] == data['west_projects'], 'Build provenance differs')
    checked = check_image(files['binary'], files['elf'], files['config'])
    require(data['source']['git_commit'][:12].encode() in files['binary'].read_bytes(),
            'Source tag absent from image')
    # A new directory prevents accidentally combining ABI evidence from another image.
    abi = output.parent / (output.stem + '-errno')
    prepare_errno(build, inputs, abi, source_suffix='/transaction.c')
    files['errno'] = abi / 'errno-map.json'
    provenance_files = {}
    for artifact in provenance['artifacts']:
        relative = Path(artifact['path'])
        require(not relative.is_absolute() and '..' not in relative.parts, 'Unsafe provenance path')
        path = repo / relative
        require(path.is_file() and path.stat().st_size == artifact['size_bytes'] and
                sha(path) == artifact['sha256'], 'Stale build provenance artifact')
        require(artifact['path'] not in provenance_files, 'Duplicate build provenance artifact')
        provenance_files[artifact['path']] = str(path.resolve())
    require(set(str(files[k].resolve()) for k in ('binary', 'elf', 'config', 'map', 'dts')) <=
            set(provenance_files.values()), 'Incomplete build provenance artifacts')
    tools = list(default_tools(repo) if tool_paths is None else tool_paths)
    require(tools, 'No frozen runner/tool identities')
    paths = list(files.values()) + sorted(abi.iterdir()) + tools + [Path(p) for p in provenance_files.values()]
    unique = sorted({p.resolve(strict=True) for p in paths})
    record = dict(schema=1, profile='m5-network-physical', target='mimxrt1060_evk@B//qspi',
                  source=data['source'], west_projects=data['west_projects'], image=checked,
                  files={k: str(v.resolve()) for k, v in files.items()},
                  tools=[str(p.resolve()) for p in tools], provenance_files=provenance_files,
                  artifacts=[dict(path=str(p), size_bytes=p.stat().st_size, sha256=sha(p)) for p in unique])
    require(source_snapshot(repo) == data['source'], 'Source changed during preparation')
    output.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    verify(output, sha(output))
    return record


def verify(manifest, expected_sha256):
    manifest = Path(manifest)
    require(re.fullmatch(r'[0-9a-f]{64}', expected_sha256) is not None and
            sha(manifest) == expected_sha256, 'Approval manifest digest differs')
    record = json.loads(manifest.read_text())
    require(record['schema'] == 1 and record['profile'] == 'm5-network-physical' and
            record['target'] == 'mimxrt1060_evk@B//qspi', 'Unexpected image manifest')
    frozen_inputs(record)
    require(set(record['files']) == ROLES, 'Incomplete image roles')
    paths = []
    for artifact in record['artifacts']:
        path = Path(artifact['path'])
        require(path.is_absolute() and path.is_file(), 'Missing/nonabsolute evidence artifact')
        require(path.stat().st_size == artifact['size_bytes'] and sha(path) == artifact['sha256'],
                'Frozen artifact changed: ' + path.name)
        paths.append(str(path))
    require(len(paths) == len(set(paths)), 'Duplicate evidence artifact')
    required = set(record['files'].values()) | set(record['tools']) | set(record['provenance_files'].values())
    require(record['tools'] and len(record['tools']) == len(set(record['tools'])) and
            required <= set(paths), 'Missing/duplicate tool or image evidence')
    files = record['files']
    inputs = json.loads(Path(files['inputs']).read_text())
    require(inputs['source'] == record['source'] and inputs['west_projects'] == record['west_projects'],
            'Provenance identity differs')
    provenance = json.loads(Path(files['provenance']).read_text())
    require(provenance['source'] == record['source'] and
            provenance['west_projects'] == record['west_projects'], 'Build provenance differs')
    provenance_paths = set()
    for artifact in provenance['artifacts']:
        relative = artifact['path']
        require(relative in record['provenance_files'] and relative not in provenance_paths,
                'Incomplete/duplicate build provenance path')
        provenance_paths.add(relative)
        actual = Path(record['provenance_files'][relative])
        require(actual.stat().st_size == artifact['size_bytes'] and sha(actual) == artifact['sha256'],
                'Stale build provenance artifact')
    require(provenance_paths == set(record['provenance_files']) and
            {files[k] for k in ('binary', 'elf', 'config', 'map', 'dts')} <=
            set(record['provenance_files'].values()), 'Incomplete build provenance artifacts')
    checked = check_image(files['binary'], files['elf'], files['config'])
    require(checked == record['image'] and record['source']['git_commit'][:12].encode() in
            Path(files['binary']).read_bytes(), 'Image geometry/source identity differs')
    image = dict(source=record['source'], binary=files['binary'], artifacts=record['artifacts'])
    record['errno_values'] = verify_errno_map(files['errno'], image, source_suffix='/transaction.c')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    create = actions.add_parser('prepare')
    for name in ('build', 'inputs', 'output', 'repo'):
        create.add_argument('--' + name, type=Path, required=True)
    check = actions.add_parser('verify')
    check.add_argument('--manifest', type=Path, required=True)
    check.add_argument('--sha256', required=True)
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare(args.build, args.inputs, args.output, args.repo)
        print(json.dumps(dict(manifest_sha256=sha(args.output))))
    else:
        result = verify(args.manifest, args.sha256)
        print(json.dumps(dict(profile=result['profile'], source=result['source']['git_commit'],
                              binary_sha256=sha(result['files']['binary']))))


if __name__ == '__main__':
    main()
