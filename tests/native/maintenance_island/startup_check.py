"""Narrow witnesses for this pinned EVKB ELF32 device/init representation.

Generated DT macros identify the actual devices; no ordinal is stable. Zephyr
keeps deferred devices in its init table and skips them using device.flags.
Unknown mappings/layouts/profiles fail instead of inferring startup safety.
"""
import re
import struct

NATIVE = (
    ('flexspi', 'DT_N_S_soc_S_spi_402a8000', 'nxp_imx_flexspi', 'memc_flexspi'),
    ('ext_flash_ctrl', 'DT_N_S_soc_S_spi_402a8000_S_flash_controller_0',
     'nxp_imx_flexspi_nor', 'flash_flexspi_nor'),
)


def native_startup_entries(header, config, symbols, read):
    def macro(name):
        values = re.findall(r'^#define\s+' + re.escape(name) + r'\s+(\S+)[ \t]*$', header, re.M)
        if len(values) != 1:
            raise ValueError('missing or ambiguous generated DT macro: ' + name)
        return values[0]

    if config.splitlines().count('CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"') != 1:
        raise ValueError('unknown board profile')
    # Pinned ARM32 struct device: six pointers, then flags at byte 24. A deinit
    # member would change that layout; do not silently interpret another ABI.
    if '# CONFIG_DEVICE_DEINIT_SUPPORT is not set' not in config.splitlines():
        raise ValueError('unknown device operations layout')
    storage = [f'CONFIG_{name}=y' in config.splitlines()
               for name in ['FILE_SYSTEM', 'SETTINGS_NVS']]
    backends = [bool(re.search(r'^#define DT_N_NODELABEL_candidate_' + owner +
                              r'_backend\s+', header, re.M))
                for owner in ['file', 'scratch', 'nvs']]
    if storage == [True, True] and all(backends):
        deferred = True
    elif storage == [False, False] and not any(backends):
        deferred = False  # Known earlier compile-only island profile.
    else:
        raise ValueError('unknown storage/deferred-owner profile')

    result = {}
    try:
        for label, node, compat, driver in NATIVE:
            if (macro('DT_N_NODELABEL_' + label) != node or
                    macro(node + '_STATUS_okay') != '1' or
                    macro(node + '_COMPAT_MATCHES_' + compat) != '1'):
                raise ValueError('unexpected native device mapping: ' + label)
            ordinal = macro(node + '_ORD')
            if not ordinal.isdecimal():
                raise ValueError('invalid generated ordinal: ' + label)
            if macro(node + '_P_zephyr_deferred_init') != str(int(deferred)):
                raise ValueError('native DT deferral differs from selected profile: ' + label)
            dev = symbols['__device_dts_ord_' + ordinal]
            entry = symbols['__init___device_dts_ord_' + ordinal]
            fields = read(dev, 25)
            _, cfg, api, _, data, init = struct.unpack('<6I', fields[:24])
            expected_api = symbols[driver + '_api'] if label == 'ext_flash_ctrl' else 0
            if (cfg != symbols[driver + '_config_0'] or api != expected_api or
                    data != symbols[driver + '_data_0'] or init != symbols[driver + '_init']):
                raise ValueError('actual native device identity differs: ' + label)
            if len(fields) != 25 or fields[24] != int(deferred):
                raise ValueError('actual native deferred flags differ: ' + label)
            if struct.unpack('<2I', read(entry, 8)) != (0, dev):
                raise ValueError('init record does not reference actual native device: ' + label)
            if not (symbols['__init_POST_KERNEL_start'] <= entry <
                    symbols['__init_APPLICATION_start'] and
                    entry > symbols['__init_crux_island_relocation_init']):
                raise ValueError('native init record is not safely after early verifier: ' + label)
            result[label] = {'device': dev, 'entry': entry, 'deferred': deferred}
    except (KeyError, IndexError, struct.error) as error:
        raise ValueError('missing/invalid actual native device or init bytes') from error
    return result


def verify_deferred_skip(body):
    # Exact pinned ARM witness: entry.dev -> r0; flags byte24 bit0 branches
    # over do_device_init to the next 8-byte init entry. Reject other codegen.
    instructions = [(int(m[1], 16), m[2].strip()) for m in re.finditer(
        r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+){1,2}([a-z][^\n]*)$', body, re.M)]
    direct_calls = [insn for _, insn in instructions if re.match(r'bl(?:\.w)?\s', insn)]
    indirect_calls = [insn for _, insn in instructions if re.match(r'blx\s', insn)]
    if (len(direct_calls) != 1 or
            not re.fullmatch(r'bl\s+[0-9a-f]+\s+<do_device_init>', direct_calls[0]) or
            any(not re.fullmatch(r'blx\s+r3', insn) for insn in indirect_calls) or
            len(indirect_calls) > 1):
        raise ValueError('unknown or additional kernel initialization call')
    starts = [i for i, (_, insn) in enumerate(instructions)
              if re.fullmatch(r'ldrb\s+r3,\s*\[r0,\s*#24\]', insn)]
    if len(starts) == 1:
        i = starts[0]
        if 2 <= i and i + 4 < len(instructions):
            previous = instructions[i - 2:i]
            shift, branch, call, advance = instructions[i + 1:i + 5]
            target = re.fullmatch(r'bmi(?:\.[nw])?\s+([0-9a-f]+)\s+<[^>]+>', branch[1])
            if (re.fullmatch(r'ldr\s+r0,\s*\[r4,\s*#4\]', previous[0][1]) and
                    re.match(r'cbz\s+r0,', previous[1][1]) and
                    re.fullmatch(r'lsls\s+r3,\s*r3,\s*#31', shift[1]) and target and
                    int(target[1], 16) == advance[0] and
                    re.fullmatch(r'bl\s+[0-9a-f]+\s+<do_device_init>', call[1]) and
                    re.fullmatch(r'adds\s+r4,\s*#8', advance[1])):
                return
    raise ValueError('missing known kernel deferred-init skip witness')
