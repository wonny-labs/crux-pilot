"""Stage only the approved experimental 100 MHz boot FCB; upstream stays read-only."""
import hashlib
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('candidate_publish', HERE / 'prepare.py')
PUBLISH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PUBLISH)
FCB = Path('zephyr/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.c')
PINNED = {
    FCB: '0ea7cf9246bac6643cad06396ff3f518ebf34b7793666646847e736eacb8c5f3',
    FCB.with_suffix('.h'): 'fc670f88c1b3e2ceb0d05b1cba944694e9e9265a320277e9981436f7e2dfe05c',
}
WITNESSES = '''/* Candidate layout witnesses. */
#include <stddef.h>
_Static_assert(offsetof(flexspi_nor_config_t, mem_config.serial_clk_freq) == 0x46,
               "FCB serial clock byte offset changed");
_Static_assert(FLEXSPI_SERIAL_CLK_100MHZ == 6, "FCB 100 MHz encoding changed");
_Static_assert(sizeof(flexspi_nor_config_t) == 512, "FCB size changed");
'''


def prepare(workspace, output):
    workspace, output = Path(workspace).resolve(), Path(output).resolve()
    for upstream in [workspace / 'zephyr', workspace / 'modules']:
        if output.is_relative_to(upstream):
            raise ValueError('FCB output must be outside pinned upstream trees')
    metadata = {}
    inputs = {}
    for relative, expected in PINNED.items():
        data = (workspace / relative).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected:
            raise ValueError('unexpected pinned FCB source: ' + str(relative))
        inputs[relative] = data
        metadata[relative.name] = {'source_sha256': digest}
    source = inputs[FCB].decode()
    if source.count('FLEXSPI_SERIAL_CLK_120MHZ') != 1:
        raise ValueError('unexpected FCB clock initializer')
    candidate = source.replace('FLEXSPI_SERIAL_CLK_120MHZ', 'FLEXSPI_SERIAL_CLK_100MHZ') + WITNESSES
    metadata[FCB.name]['candidate_sha256'] = hashlib.sha256(candidate.encode()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    PUBLISH.publish(output, FCB.name, candidate)
    PUBLISH.publish(output, 'fcb-source-hashes.json', json.dumps(metadata, indent=2) + '\n')
    return metadata


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.workspace, args.output)
