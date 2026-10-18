"""Prepare a read-only image manifest after frozen software verification; no hardware."""
import argparse
import hashlib
import json
from pathlib import Path
from m3_image import check_image, require


def prepare(build, inputs, profile, output):
    folder = build / 'zephyr'
    paths = {'binary': folder / 'zephyr.bin', 'elf': folder / 'zephyr.elf', 'config': folder / '.config'}
    checked = check_image(paths['binary'], paths['elf'], paths['config'], profile)
    data = json.loads(inputs.read_text())
    require(not data['source']['dirty'] and data['source']['recoverable_from_git'], 'Unfrozen build inputs')
    expected = {'zephyr': '684c9e8f32e4373a21098559f748f06915f950c9',
                'cmsis': '512cc7e895e8491696b61f7ba8066b4a182569b8',
                'cmsis_6': '30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74',
                'hal_nxp': '2c2f28ac333e995d7279777409817c4b4f92c1ec'}
    require({p['name'] for p in data['west_projects']} == set(expected), 'Unexpected dependencies')
    for project in data['west_projects']:
        require(project['git_commit'] == project['revision'] == expected[project['name']] and not project['dirty'],
                'Dependency changed')
    require(data['source']['git_commit'][:12].encode() in paths['binary'].read_bytes(), 'Source tag absent from image')
    records = []
    for path in list(paths.values()) + [folder / 'zephyr.map', folder / 'zephyr.dts', build / 'compile_commands.json', inputs]:
        records.append({'path': str(path.resolve()), 'size_bytes': path.stat().st_size,
                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    manifest = dict(checked, target='mimxrt1060_evk@B//qspi', profile=profile,
                    source=data['source'], west_projects=data['west_projects'],
                    artifacts=records, **{k: str(v.resolve()) for k, v in paths.items()})
    output.write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--profile', choices=('probe', 'normal', 'network', 'integrated-probe', 'integrated-normal'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.build, args.inputs, args.profile, args.output)
