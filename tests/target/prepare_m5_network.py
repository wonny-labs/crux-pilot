"""Freeze/verify an M5 test image and software evidence. Never access hardware.

The manifest digest must be supplied independently by the physical approval record.
The manifest and compiler/header originals are private evidence: they contain paths.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re

from image_profile import ImageProfile
from m3_errno import verify_errno_map
from m3_image import require
from m5_image import check_image
from prepare_m3_errno import prepare as prepare_errno
import s2_image

_SPEC = importlib.util.spec_from_file_location(
    'm5_build_provenance', Path(__file__).resolve().parents[2] / 'scripts/record-build-provenance.py')
_PROVENANCE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_PROVENANCE)
source_snapshot = _PROVENANCE.source_snapshot
PINNED = {
    'zephyr': 'dccb09599635bdff17633fa7e9dab014b91dce90',
    'cmsis': '512cc7e895e8491696b61f7ba8066b4a182569b8',
    'cmsis_6': '30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74',
    'hal_nxp': '7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0',
}
# Where the build finds each pinned dependency (scripts/test-networking-physical.sh names these directories).
PINNED_PATHS = {'zephyr': 'zephyr', 'cmsis': 'modules/hal/cmsis', 'cmsis_6': 'modules/hal/cmsis_6',
                'hal_nxp': 'modules/hal/nxp'}
MANIFEST_PATH = 'crux-pilot'
ROLES = {'binary', 'elf', 'config', 'map', 'dts', 'compilation', 'inputs', 'provenance', 'errno'}
M5_NAME = 'm5-network-physical'


def m5_profile():
    """The M5 test image. Built at call time so that these module constants stay the single source of its facts."""
    return ImageProfile(name=M5_NAME, manifest_path=MANIFEST_PATH, pinned=PINNED, pinned_paths=PINNED_PATHS,
                        check_image=check_image, commit_tag=True, errno_suffix='/transaction.c',
                        tool_scripts=('test-networking-physical.sh', 'record-build-provenance.py'))


S2_NAME = 's2-controller-physical'
S2_MANIFEST_PATH = 'crux-controller'
# The S2 build uses the same four dependencies at the same west paths as the M5 build. They are listed again so
# that a later change of the M5 pins does not silently move the S2 image.
S2_PINNED = {
    'zephyr': 'dccb09599635bdff17633fa7e9dab014b91dce90',
    'cmsis': '512cc7e895e8491696b61f7ba8066b4a182569b8',
    'cmsis_6': '30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74',
    'hal_nxp': '7554bc0f6f6c284972eba7b2c17b16fbb9cc0ad0',
}
S2_PINNED_PATHS = {'zephyr': 'zephyr', 'cmsis': 'modules/hal/cmsis', 'cmsis_6': 'modules/hal/cmsis_6',
                   'hal_nxp': 'modules/hal/nxp'}
# The S2 build finds its modules by west module discovery (zephyr.meta lists these four besides the dependencies
# above), so each is part of the image and is pinned exactly.
S2_MODULES = {
    'crux-pilot': 'db5adca28bd313caaf3899e959920fac4d508699',
    'picolibc': '01254932e8e81085817ed61fd858648584ffe37c',
    'fatfs': 'f4ead3bf4a6dab3a07d7b5f5315795c073db568d',
    'littlefs': '8f5ca347843363882619d8f96c00d8dbd88a8e79',
}
S2_MODULE_PATHS = {'crux-pilot': 'crux-pilot', 'picolibc': 'modules/lib/picolibc', 'fatfs': 'modules/fs/fatfs',
                   'littlefs': 'modules/fs/littlefs'}


# The window of the raw UART capture after the S2 start command. A proposal of the crux-controller plan (180 s: the
# workload starts only after the network init wait of up to 30 s), bounded by MAX_CAPTURE_SECONDS; the owner decides it.
S2_CAPTURE_SECONDS = 180


def firmware_identity_of(image_sha256, config_bytes, target=s2_image.TARGET):
    """The firmware identity digest as scripts/record-build-provenance.py computes it, from the recorder's own functions."""
    identity = dict(algorithm=_PROVENANCE.FIRMWARE_IDENTITY_ALGORITHM, target=target, image_sha256=image_sha256,
                    configuration_sha256=hashlib.sha256(_PROVENANCE.normalised_config(config_bytes)).hexdigest())
    return _PROVENANCE.firmware_digest(identity)


def s2_check_image(binary, elf, config):
    """The S2 checks plus the firmware identity, which the manifest records because the binary has no commit tag."""
    facts = s2_image.check_image(binary, elf, config)
    facts['firmware_identity'] = firmware_identity_of(sha(binary), Path(config).read_bytes())
    return facts


def s2_profile():
    """The crux-controller S2 image: no commit tag, no errno evidence, modules found by west module discovery."""
    return ImageProfile(name=S2_NAME, manifest_path=S2_MANIFEST_PATH, pinned=S2_PINNED, pinned_paths=S2_PINNED_PATHS,
                        check_image=s2_check_image, commit_tag=False, errno_suffix=None,
                        tool_scripts=('record-build-provenance.py',), modules=S2_MODULES, module_paths=S2_MODULE_PATHS,
                        capture='raw', capture_seconds=S2_CAPTURE_SECONDS)


PROFILES = {M5_NAME: m5_profile, S2_NAME: s2_profile}


def profile_named(name):
    """A registered profile. Unknown names are refused; a new image adds its factory to PROFILES."""
    require(name in PROFILES, 'Unknown image profile: ' + str(name))
    profile = PROFILES[name]()
    require(profile.name == name, 'Registered image profile name differs from its key: ' + str(name))
    return profile


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def frozen_inputs(data, profile=None):
    profile = profile or m5_profile()
    pinned, pinned_paths = profile.locked()
    source = data['source']
    require(not source['dirty'] and source['recoverable_from_git'], 'Unfrozen build inputs')
    require(re.fullmatch(r'[0-9a-f]{40}', source['git_commit']) is not None and
            re.fullmatch(r'[0-9a-f]{64}', source['snapshot_sha256']) is not None,
            'Incomplete source identity')
    projects = data['west_projects']
    # Linked worktrees may record west's distinct manifest checkout. It is workspace
    # bootstrap context, not the built source: source above identifies that worktree.
    contexts = [p for p in projects if p['name'] == 'manifest']
    dependencies = [p for p in projects if p['name'] != 'manifest']
    locked = [p for p in dependencies if p['name'] in pinned]
    others = [p for p in dependencies if p['name'] not in pinned]
    # The pinned dependencies are exact and each appears once. An image whose build names its modules explicitly
    # (M5, see scripts/test-networking-physical.sh) records any other project the manifest makes active but never
    # builds it; it must still be a clean, resolved checkout. An image built by west module discovery (profile
    # modules) builds every active project that carries Zephyr module metadata, and the freeze cannot see which
    # do, so its modules are pinned the same way and no other project is accepted.
    require(len(contexts) <= 1 and len(locked) == len(pinned) and {p['name'] for p in locked} == set(pinned),
            'Unexpected/missing/duplicate dependencies or manifest context')
    require(not (profile.modules and others),
            'Unpinned west project in an image built by module discovery, where it would be a build module: ' +
            ', '.join(sorted(str(p.get('name')) for p in others)))
    fields = {'name', 'path', 'revision', 'git_commit', 'dirty'}
    require(len({p['name'] for p in others}) == len(others), 'Duplicate other west project')
    reserved = [PurePosixPath(profile.manifest_path)] + [PurePosixPath(v) for v in pinned_paths.values()]
    seen = set()
    for project in others:
        revision, path = project.get('revision'), project.get('path')
        require(set(project) == fields and project['dirty'] is False and
                re.fullmatch(r'[0-9a-f]{40}', str(project['git_commit'])) is not None and
                isinstance(revision, str) and revision not in ('', 'HEAD') and
                (re.fullmatch(r'[0-9a-fA-F]{40}', revision) is None or revision == project['git_commit']) and
                isinstance(path, str) and path != '' and '\\' not in path and not path.startswith('/') and
                '..' not in PurePosixPath(path).parts and '.' not in path.split('/')[:-1] and
                PurePosixPath(path).parts not in ((), ('.',)),
                'Other west project is dirty, unresolved or misplaced: ' + str(project.get('name')))
        where = PurePosixPath(path)
        # No other project may be, contain or sit inside the manifest checkout or a directory the build compiles.
        require(not any(where == r or r in where.parents or where in r.parents for r in reserved) and where not in seen,
                'Other west project overlaps a pinned dependency or the manifest checkout: ' + str(project.get('name')))
        seen.add(where)
    for context in contexts:
        require(set(context) == {'name', 'path', 'revision', 'git_commit', 'dirty'} and
                context['path'] == profile.manifest_path and context['revision'] == 'HEAD' and
                context['dirty'] is False and
                re.fullmatch(r'[0-9a-f]{40}', context['git_commit']) is not None,
                'Unfrozen/noncanonical workspace manifest context')
    for project in locked:
        require(set(project) == fields and project['revision'] == project['git_commit'] == pinned[project['name']] and
                project['dirty'] is False and project['path'] == pinned_paths[project['name']], 'Dependency changed')


def default_tools(repo, profile=None):
    # Freeze executable physical tooling separately from the complete source snapshot.
    profile = profile or m5_profile()
    paths = [p for p in (repo / 'tests/target').glob('*.py') if p.is_file()]
    paths.extend((repo / 'scripts' / name) for name in profile.tool_scripts)
    return sorted(paths)


def prepare(build, inputs, output, repo, *, tool_paths=None, profile=None, tool_repo=None):
    """``repo`` holds the image source; ``tool_repo`` (default: ``repo``) holds the frozen board tooling."""
    profile = profile or m5_profile()
    build, inputs, output, repo = map(Path, (build, inputs, output, repo))
    tool_repo = Path(tool_repo) if tool_repo else repo
    if output.exists():
        raise FileExistsError(output)
    data = json.loads(inputs.read_text())
    frozen_inputs(data, profile)
    require(source_snapshot(repo) == data['source'], 'Source changed since input capture')
    folder = build / 'zephyr'
    files = dict(binary=folder / 'zephyr.bin', elf=folder / 'zephyr.elf',
                 config=folder / '.config', map=folder / 'zephyr.map',
                 dts=folder / 'zephyr.dts', compilation=build / 'compile_commands.json', inputs=inputs,
                 provenance=build / 'provenance.json')
    provenance = json.loads(files['provenance'].read_text())
    require(provenance['source'] == data['source'] and
            provenance['west_projects'] == data['west_projects'], 'Build provenance differs')
    checked = profile.check_image(files['binary'], files['elf'], files['config'])
    require(not profile.commit_tag or data['source']['git_commit'][:12].encode() in files['binary'].read_bytes(),
            'Source tag absent from image')
    # A new directory prevents accidentally combining ABI evidence from another image.
    abi = output.parent / (output.stem + '-errno')
    if profile.errno_suffix:
        prepare_errno(build, inputs, abi, source_suffix=profile.errno_suffix)
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
    tools = list(default_tools(tool_repo, profile) if tool_paths is None else tool_paths)
    require(tools, 'No frozen runner/tool identities')
    paths = list(files.values()) + (sorted(abi.iterdir()) if profile.errno_suffix else []) + tools + [Path(p) for p in provenance_files.values()]
    unique = sorted({p.resolve(strict=True) for p in paths})
    record = dict(schema=1, profile=profile.name, target='mimxrt1060_evk@B//qspi',
                  source=data['source'], west_projects=data['west_projects'], image=checked,
                  files={k: str(v.resolve()) for k, v in files.items()},
                  tools=[str(p.resolve()) for p in tools], provenance_files=provenance_files,
                  artifacts=[dict(path=str(p), size_bytes=p.stat().st_size, sha256=sha(p)) for p in unique])
    require(source_snapshot(repo) == data['source'], 'Source changed during preparation')
    output.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    verify(output, sha(output), profile)
    return record


def verify(manifest, expected_sha256, profile=None):
    profile = profile or m5_profile()
    manifest = Path(manifest)
    require(re.fullmatch(r'[0-9a-f]{64}', expected_sha256) is not None and
            sha(manifest) == expected_sha256, 'Approval manifest digest differs')
    record = json.loads(manifest.read_text())
    require(record['schema'] == 1 and record['profile'] == profile.name and
            record['target'] == 'mimxrt1060_evk@B//qspi', 'Unexpected image manifest')
    frozen_inputs(record, profile)
    roles = ROLES if profile.errno_suffix else ROLES - {'errno'}
    require(set(record['files']) == roles, 'Incomplete image roles')
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
    checked = profile.check_image(files['binary'], files['elf'], files['config'])
    require(checked == record['image'] and (not profile.commit_tag or record['source']['git_commit'][:12].encode() in
            Path(files['binary']).read_bytes()), 'Image geometry/source identity differs')
    image = dict(source=record['source'], binary=files['binary'], artifacts=record['artifacts'])
    if profile.errno_suffix:
        record['errno_values'] = verify_errno_map(files['errno'], image, source_suffix=profile.errno_suffix)
    return record


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    create = actions.add_parser('prepare')
    for name in ('build', 'inputs', 'output', 'repo'):
        create.add_argument('--' + name, type=Path, required=True)
    create.add_argument('--tool-repo', type=Path, default=None,
                        help='repository whose tests/target and scripts are frozen as the board tooling (default: --repo)')
    check = actions.add_parser('verify')
    check.add_argument('--manifest', type=Path, required=True)
    check.add_argument('--sha256', required=True)
    for action in (create, check):
        action.add_argument('--profile', choices=sorted(PROFILES), default=M5_NAME,
                            help='image profile (image facts only; the hardware safety gates are the same for every profile)')
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    profile = profile_named(args.profile)
    if args.action == 'prepare':
        prepare(args.build, args.inputs, args.output, args.repo, profile=profile, tool_repo=args.tool_repo)
        print(json.dumps(dict(manifest_sha256=sha(args.output))))
    else:
        result = verify(args.manifest, args.sha256, profile)
        print(json.dumps(dict(profile=result['profile'], source=result['source']['git_commit'],
                              binary_sha256=sha(result['files']['binary']))))


if __name__ == '__main__':
    main()
