"""One read-only diagnostic composition of the accepted private source copies.

No edits to upstream. Native device initialization/probing and arbitrary command
helpers are not emitted. HAL transfer admission is narrowed to status/ID reads.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('command_stage', ROOT / 'tests/native/flash_target_candidate/stage_commands.py')
COMMANDS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMANDS)


def definition(source, name):
    match = re.search(r'^(?:static )?(?:inline )?\w+\s+' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError('selected function changed: ' + name)
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def read_helpers(source):
    COMMANDS.BASE.BASE.transform_nor(source)  # exact pinned source check
    helpers = COMMANDS.selected_helpers(source)
    # Extract only two existing helpers from the already guarded command stage.
    result = []
    for name in ['crux_rom_native_read_status', 'crux_rom_native_read_id_helper']:
        start, end = COMMANDS.BASE.function_span(helpers, name)
        result.append(helpers[start:end])
    return '\n'.join(result) + '\n'


def diagnostic_hal(source):
    result = COMMANDS.deadline_waits(COMMANDS.BASE.transform_hal(source), False)
    anchor = 'status_t FLEXSPI_TransferBlocking(FLEXSPI_Type *base, flexspi_transfer_t *xfer)\n{'
    guard = '''
    if (xfer == NULL) { crux_flash_poc_terminal(-EACCES); }
    flexspi_transfer_t observation = *xfer;
    xfer = &observation;
    /* This separately built diagnostic cannot issue arbitrary read/write commands. */
    if (xfer->cmdType != kFLEXSPI_Read || xfer->port != kFLEXSPI_PortA1 ||
        xfer->deviceAddress != 0 || xfer->SeqNumber != 1 || xfer->data == NULL ||
        !((xfer->seqIndex == 1 && xfer->dataSize == 1) ||
          (xfer->seqIndex == 15 && xfer->dataSize == 3))) {
        crux_flash_poc_terminal(-EACCES);
    }
'''
    result = COMMANDS.BASE.replace_once(result, anchor, anchor + guard)
    # The native transaction is read-only in this composition. Keep the actual
    # read body and completion waits, but do not emit a write dispatch branch.
    transfer = definition(result, 'FLEXSPI_TransferBlocking')
    branch = re.search(r'    if \(\(xfer->cmdType == kFLEXSPI_Write\).*?    /\* Wait until the IP command execution finishes \*/', transfer, re.S)
    if not branch:
        raise ValueError('SDK transfer dispatch branch changed')
    read = '    result = FLEXSPI_ReadBlocking(base, (uint8_t *)xfer->data, xfer->dataSize);\n\n'
    changed = transfer[:branch.start()] + read + '    /* Wait until the IP command execution finishes */' + transfer[branch.end():]
    result = COMMANDS.BASE.replace_once(result, transfer, changed)

    # Full-file relocation retains emitted functions; emit only the selected
    # actual SDK graph so controller reset/config/write entry points are absent.
    prelude = result[:result.index('/*******************************************************************************\n * Prototypes')]
    array = 'static FLEXSPI_Type *const s_flexspiBases[] = FLEXSPI_BASE_PTRS;\n'
    if source.count(array.strip()) != 1:
        raise ValueError('SDK base array changed')
    return prelude + array + '\n'.join(definition(result, name) for name in [
        'FLEXSPI_GetInstance', 'FLEXSPI_CheckInputLutLocation', 'FLEXSPI_CheckAndClearError',
        'FLEXSPI_ReadBlocking', 'FLEXSPI_TransferBlocking', 'FLEXSPI_UpdateLUT']) + '\n'


def diagnostic_island(source):
    if hashlib.sha256(source.encode()).hexdigest() != '4151357ff008eef5badd4d36d1acd4df37118006e36a624f1fba339602e19216':
        raise ValueError('selected private island input changed')
    source = COMMANDS.BASE.replace_once(source, 'int crux_flash_island_inspect_and_refuse(void)',
                                        'int crux_flash_island_prepare(void)')
    return COMMANDS.BASE.replace_once(source,
        '    /* Return establishes only a completed unchanged read-only observation. */\n'
        '    (void)crux_island_enter();\n    return -EACCES;', '    return 0;')


def diagnostic_profile(source):
    if hashlib.sha256(source.encode()).hexdigest() != '9ef50f5808cc456165877cda4fde746e9591b6d8fbeaca5da6961821d482ed1c':
        raise ValueError('selected private profile input changed')
    source = source.replace('bool crux_rom_read_confirm_supported(void)',
                            'bool crux_rom_read_capture_supported(struct crux_rom_profile *observation)', 1)
    source = source.replace('    if (!crux_rom_clock_inputs_supported(&p)',
                            '    bool supported = false;\n    if (!crux_rom_clock_inputs_supported(&p)', 1)
    source = source.replace('        return false;', '        goto observed;')
    return COMMANDS.BASE.replace_once(source, '    return crux_rom_profile_supported(&p);',
        '    supported = crux_rom_profile_supported(&p);\n'
        'observed:\n    if (observation) { *observation = p; }\n    return supported;') + (
        '\nbool crux_rom_read_confirm_supported(void) {\n'
        '    return crux_rom_read_capture_supported(0);\n}\n')


def prepare(workspace, output):
    workspace, output = Path(workspace).resolve(), Path(output).resolve()
    if any(output.is_relative_to(workspace / path) for path in ['zephyr', 'modules']):
        raise ValueError('diagnostic output must be outside pinned upstream')
    nor_path = workspace / 'zephyr/drivers/flash/flash_mcux_flexspi_nor.c'
    memc_path = workspace / 'zephyr/drivers/memc/memc_mcux_flexspi.c'
    hal_path = workspace / 'modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c'
    nor, memc, hal = (path.read_text() for path in [nor_path, memc_path, hal_path])
    # Exact same pinned transformations validate all original input before output.
    base_memc = COMMANDS.deadline_waits(COMMANDS.BASE.transform_memc(memc), True)
    # Keep original includes, metadata structures and get_base only, then select
    # the actual bounded idle + transfer bodies. No native SYS_INIT/device table.
    prelude = base_memc[:base_memc.index('static inline uint8_t *get_ahb')]
    selected_memc = prelude + '\n'.join(definition(base_memc, name) for name in [
        'memc_flexspi_wait_bus_idle', 'memc_flexspi_transfer'])
    selected_memc += '\n#include "rom_command_memc.inc"\n#include "rom_identity_memc.inc"\n'
    # Keep licensed include block and exact actual NOR data structure, without
    # full native initialization, probe LUT, write helpers or driver registration.
    nor_prelude = nor[:nor.index('LOG_MODULE_REGISTER(')]
    match = re.search(r'struct flash_flexspi_nor_data \{.*?\n\};', nor, re.S)
    if not match:
        raise ValueError('native NOR metadata changed')
    selected_nor = nor_prelude + 'LOG_MODULE_REGISTER(observation_native, 0);\n' + match[0] + '\n' + read_helpers(nor)
    selected_nor += '\n#include "observation_nor.inc"\n'
    generated = {'observation_native.c': selected_nor, 'memc_mcux_flexspi.c': selected_memc,
                 'fsl_flexspi.c': diagnostic_hal(hal)}
    sources = {'observation_native.c': nor_path, 'memc_mcux_flexspi.c': memc_path, 'fsl_flexspi.c': hal_path}
    for name, transform in [('island.c', diagnostic_island), ('rom_profile.c', diagnostic_profile)]:
        sources[name] = ROOT / 'samples/flash_target_candidate' / name
        generated[name] = transform(sources[name].read_text())
    clock_path = workspace / 'zephyr/soc/nxp/imxrt/imxrt10xx/flexspi.c'
    if hashlib.sha256(clock_path.read_bytes()).hexdigest() != 'e79fba725e3eb84faab99614908e1d5ead53ba90df437d52323fbac67ab629c7':
        raise ValueError('selected SoC FlexSPI clock setter changed')
    # The native clock device's generic set-rate path is retained by its API
    # table; it must not retain clock/controller-reset capability here.
    sources['flexspi.c'] = clock_path
    generated['flexspi.c'] = ('/* SPDX-License-Identifier: Apache-2.0 */\n'
        '#include <stdint.h>\n#include <errno.h>\n'
        'uint32_t flexspi_clock_set_freq(uint32_t clock_name, uint32_t rate) {\n'
        '    (void)clock_name; (void)rate; return (uint32_t)-EACCES;\n}\n')
    output.mkdir(parents=True, exist_ok=True)
    metadata = {}
    for name, text in generated.items():
        COMMANDS.BASE.publish(output, name, text)
        metadata[name] = {'source_sha256': hashlib.sha256(sources[name].read_bytes()).hexdigest(),
                          'diagnostic_sha256': hashlib.sha256(text.encode()).hexdigest()}
    COMMANDS.BASE.publish(output, 'observation-source-hashes.json', json.dumps(metadata, indent=2) + '\n')
    return metadata


if __name__ == '__main__':
    import sys
    prepare(sys.argv[1], sys.argv[2])
