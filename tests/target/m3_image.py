"""Pure ELF/BIN/config checks for the identified EVKB health images."""
import struct
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_image_bytes(binary_path, elf_path):
    binary = Path(binary_path).read_bytes()
    elf = Path(elf_path).read_bytes()
    require(len(binary) > 0x2020 and binary[:4] == b'FCFB', 'Missing standard QSPI FlexSPI header')
    require(binary[0x1000:0x1004] == bytes.fromhex('d1002041'), 'Unexpected pinned IVT header')
    require(struct.unpack_from('<I', binary, 0x1014)[0] == 0x60001000 and
            struct.unpack_from('<I', binary, 0x1010)[0] == 0x60001020 and
            struct.unpack_from('<I', binary, 0x1004)[0] == 0x60002000, 'Unexpected standalone boot layout')
    require(elf[:6] == b'\x7fELF\x01\x01', 'Expected little-endian ELF32')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', elf)
    require(h[2] == 40 and h[9] == 32 and h[11] == 40, 'Expected ARM ELF layouts')
    loads = []
    for i in range(h[10]):
        kind, offset, vma, address, size, memory, flags, align = struct.unpack_from('<IIIIIIII', elf, h[5] + i * h[9])
        if kind != 1 or not size:
            continue
        require(memory >= size and 0x60000000 <= address < address + size <= 0x60800000,
                'ELF load outside verified firmware region')
        require(offset + size <= len(elf), 'Truncated ELF load')
        loads.append(dict(offset=offset, address=address, vma=vma, bytes=size))
    require(loads and min(s['address'] for s in loads) == 0x60000000, 'Wrong firmware base')
    require(max(s['address'] + s['bytes'] for s in loads) - 0x60000000 == len(binary), 'BIN span mismatch')
    sections = [struct.unpack_from('<IIIIIIIIII', elf, h[6] + i * h[11]) for i in range(h[12])]
    # Pinned objcopy --gap-fill=0xff copies allocated file-backed sections;
    # PT_LOAD alignment holes are not section data and may be zero in the ELF.
    expected = bytearray([0xff]) * len(binary)
    covered = bytearray(len(binary))
    section_count = 0
    for section in sections:
        name, kind, flags, vma, offset, size, link, info, align, entry = section
        if not flags & 2 or kind == 8 or not size:
            continue
        candidates = [s for s in loads if s['offset'] <= offset and
                      offset + size <= s['offset'] + s['bytes'] and
                      vma - s['vma'] == offset - s['offset']]
        require(len(candidates) == 1 and offset + size <= len(elf),
                'Allocated section lacks one complete matching load segment')
        segment = candidates[0]
        target = segment['address'] + vma - segment['vma'] - 0x60000000
        require(0 <= target < target + size <= len(binary) and not any(covered[target:target+size]),
                'Allocated sections overlap or leave verified image bounds')
        expected[target:target+size] = elf[offset:offset+size]
        covered[target:target+size] = bytes([1]) * size
        section_count += 1
    require(section_count and binary == expected, 'ELF sections/BIN or pinned gap-fill mismatch')
    strings_section = sections[h[13]]
    strings = elf[strings_section[4]:strings_section[4] + strings_section[5]]
    names = {strings[s[0]:].split(b'\0', 1)[0]: s for s in sections}
    require(b'rom_start' in names and names[b'rom_start'][3] == 0x60000000,
            'Missing boot header at binary base')
    symbols = {}
    for s in sections:
        if s[1] != 2:
            continue
        require(s[9] == 16, 'Wrong symbol layout')
        table = sections[s[6]]
        strings = elf[table[4]:table[4] + table[5]]
        for o in range(s[4], s[4] + s[5], 16):
            name, value, size, info, other, index = struct.unpack_from('<IIIBBH', elf, o)
            symbols[strings[name:].split(b'\0', 1)[0].decode(errors='replace')] = dict(address=value, bytes=size)
    return dict(symbols=symbols, loads=loads, binary_bytes=len(binary))


def check_image(binary_path, elf_path, config_path, profile):
    geometry = check_image_bytes(binary_path, elf_path)
    symbols, loads = geometry['symbols'], geometry['loads']
    binary = Path(binary_path).read_bytes()
    require(profile in ('probe', 'normal', 'network', 'integrated-probe', 'integrated-normal'),
            'Wrong profile')
    integrated = profile.startswith('integrated-')
    network = profile == 'network' or integrated
    worker = profile != 'network'
    commands = profile in ('probe', 'integrated-probe')
    main_bytes = 4160 if network else 2112
    if worker:
        require(symbols.get('work_stack', {}).get('bytes') == 4160, 'Unexpected worker stack object')
    else:
        require('work_stack' not in symbols, 'Unexpected controller worker in network probe')
    require(symbols.get('z_main_stack', {}).get('bytes') == main_bytes, 'Unexpected main stack object')
    if integrated:
        require(symbols.get('network_stack', {}).get('bytes') == 4160, 'Unexpected network stack object')
    config = Path(config_path).read_text().splitlines()
    if {'CONFIG_CRUX_M3_DEFERRED_OUTPUT=y', 'CONFIG_CRUX_M3_NATIVE_OUTPUT=y'}.intersection(config):
        require((profile == 'network' or (integrated and 'CONFIG_CRUX_M3_NATIVE_OUTPUT=y' in config))
                and symbols.get('logging_stack', {}).get('bytes') == 2112,
                'Unexpected native logger stack object (2048 usable plus 64 guard)')
    required = ['CONFIG_BOARD_REVISION="B"', 'CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"',
                'CONFIG_FLASH_BASE_ADDRESS=0x60000000', 'CONFIG_FLASH_SIZE=8192', 'CONFIG_XIP=y',
                'CONFIG_NXP_IMXRT_BOOT_HEADER=y', 'CONFIG_FLASH_LOAD_OFFSET=0',
                '# CONFIG_BOOTLOADER_MCUBOOT is not set', 'CONFIG_HEAP_MEM_POOL_SIZE=0', 'CONFIG_MPU_STACK_GUARD=y',
                f'CONFIG_MAIN_STACK_SIZE={4096 if network else 2048}', '# CONFIG_FPU is not set',
                'CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE=32', 'CONFIG_GPIO=y']
    if worker:
        required += ['CONFIG_CRUX_CONTROLLER_M3=y', 'CONFIG_WATCHDOG=y', 'CONFIG_HWINFO=y']
    else:
        required += ['CONFIG_CRUX_M3_NETWORK_PROBE=y']
        require('CONFIG_CRUX_CONTROLLER_M3=y' not in config, 'Conflicting controller composition')
    if network:
        required += ['CONFIG_NETWORKING=y', 'CONFIG_NET_IPV4=y', '# CONFIG_NET_IPV6 is not set',
                     'CONFIG_NET_TCP=y', 'CONFIG_NET_UDP=y', 'CONFIG_NET_SOCKETS=y',
                     '# CONFIG_NET_DHCPV4 is not set', 'CONFIG_NET_BUF_POOL_USAGE=y',
                     'CONFIG_NET_PKT_RX_COUNT=16', 'CONFIG_NET_PKT_TX_COUNT=16',
                     'CONFIG_NET_BUF_RX_COUNT=32', 'CONFIG_NET_BUF_TX_COUNT=32',
                     'CONFIG_NET_MAX_CONTEXTS=4', 'CONFIG_NET_MAX_CONN=4', 'CONFIG_ZVFS_OPEN_MAX=8']
    else:
        required += ['# CONFIG_NETWORKING is not set']
    if integrated:
        required += ['CONFIG_CRUX_M3_INTEGRATED=y']
    else:
        require('CONFIG_CRUX_M3_INTEGRATED=y' not in config, 'Unexpected integration profile')
    if commands:
        required += ['CONFIG_CRUX_M3_TEST_COMMANDS=y']
    else:
        require('CONFIG_CRUX_M3_TEST_COMMANDS=y' not in config, 'Unexpected fault-command exposure')
    if commands or profile == 'network':
        required += ['CONFIG_SHELL=y', 'CONFIG_SHELL_STACK_SIZE=2048', 'CONFIG_SHELL_BACKEND_SERIAL=y']
        required += [f'# CONFIG_{module}_SHELL is not set' for module in ('KERNEL', 'DEVICE', 'DATE', 'DEVMEM')]
    else:
        required += ['# CONFIG_SHELL is not set']
    for item in required:
        require(item in config, f'Unexpected configuration: {item}')
    return dict(elf_sections_match_bin=True, gap_fill=255, bin_address=0x60000000, bin_bytes=len(binary),
                elf_load_segments=loads, worker_stack=symbols.get('work_stack'),
                network_stack=symbols.get('network_stack'), main_stack=symbols.get('z_main_stack'),
                logger_stack=symbols.get('logging_stack'))


def check_record_output(config_path):
    """Require the reviewed output path for new network/combined physical candidates."""
    config = Path(config_path).read_text().splitlines()
    if {'CONFIG_CRUX_M3_DEFERRED_OUTPUT=y', 'CONFIG_CRUX_M3_NATIVE_OUTPUT=y'}.intersection(config):
        diagnostic = 'CONFIG_CRUX_M3_DEFERRED_OUTPUT=y' in config
        native = 'CONFIG_CRUX_M3_NATIVE_OUTPUT=y' in config
        require(diagnostic != native, 'Select exactly one native output composition')
        if diagnostic:
            require({'CONFIG_CRUX_M3_PHY_DIAGNOSTIC=y', 'CONFIG_CRUX_M3_MDIO_FAILURE_TRACE=y'}.issubset(config),
                    'Incomplete startup-only diagnostic output composition')
        else:
            require(not {'CONFIG_CRUX_M3_PHY_DIAGNOSTIC=y', 'CONFIG_CRUX_M3_MDIO_FAILURE_TRACE=y'}.intersection(config),
                    'Diagnostic observers cannot qualify the native network profile')
        integrated = 'CONFIG_CRUX_M3_INTEGRATED=y' in config
        require(not integrated or native, 'Diagnostic output cannot qualify integration')
        require(('CONFIG_CRUX_M3_NETWORK_PROBE=y' in config) != integrated,
                'Select one network or integrated native-output role')
        required = {
            'CONFIG_CRUX_M3_MDIO_COMPLETION_FIX=y',
            'CONFIG_LOG=y', 'CONFIG_LOG_PRINTK=y', 'CONFIG_LOG_MODE_DEFERRED=y',
            'CONFIG_LOG_PROCESS_THREAD=y', 'CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=2048',
            'CONFIG_LOG_PROCESS_THREAD_CUSTOM_PRIORITY=y', 'CONFIG_LOG_PROCESS_THREAD_PRIORITY=14',
            'CONFIG_LOG_PROCESS_THREAD_STARTUP_DELAY_MS=0', 'CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10',
            'CONFIG_LOG_PROCESS_TRIGGER_THRESHOLD=1',
            f'CONFIG_LOG_BUFFER_SIZE={4096 if diagnostic else 16384}',
            'CONFIG_LOG_DEFAULT_LEVEL=0', 'CONFIG_LOG_MAX_LEVEL=0', 'CONFIG_LOG_OVERRIDE_LEVEL=0',
            'CONFIG_LOG_BACKEND_UART=y', 'CONFIG_LOG_BACKEND_UART_AUTOSTART=y',
            'CONFIG_LOG_BACKEND_UART_OUTPUT_TEXT=y', 'CONFIG_THREAD_MONITOR=y', 'CONFIG_THREAD_NAME=y',
        }
        forbidden = {
            'CONFIG_PRINTK_SYNC=y', 'CONFIG_LOG_MODE_IMMEDIATE=y', 'CONFIG_LOG_MODE_MINIMAL=y',
            'CONFIG_LOG_BLOCK_IN_THREAD=y', 'CONFIG_LOG_MODE_OVERFLOW=y',
            'CONFIG_LOG_BACKEND_UART_ASYNC=y', 'CONFIG_SHELL_LOG_BACKEND=y',
            'CONFIG_CRUX_M3_CONTROLLER_NETWORK=y',
            'CONFIG_LOG_BACKEND_SHOW_TIMESTAMP=y', 'CONFIG_LOG_BACKEND_SHOW_LEVEL=y',
            'CONFIG_LOG_BACKEND_SHOW_COLOR=y', 'CONFIG_LOG_BACKEND_FORMAT_TIMESTAMP=y',
            'CONFIG_LOG_FRONTEND=y', 'CONFIG_LOG_FRONTEND_ONLY=y', 'CONFIG_LOG_MULTIDOMAIN=y',
        }
        if integrated:
            required |= {'CONFIG_CRUX_CONTROLLER_M3=y', 'CONFIG_CRUX_M3_INTEGRATED=y'}
        else:
            required.add('CONFIG_CRUX_M3_NETWORK_PROBE=y')
            forbidden.add('CONFIG_CRUX_CONTROLLER_M3=y')
        require(required.issubset(config) and not forbidden.intersection(config),
                'Unreviewed deferred diagnostic output configuration')
        # Only the native UART instance is permitted; capability/format symbols are not backends.
        backends = {line for line in config if line.startswith('CONFIG_LOG_BACKEND_') and
                    line.endswith('=y')}
        require(backends.issubset({'CONFIG_LOG_BACKEND_UART=y',
                'CONFIG_LOG_BACKEND_UART_AUTOSTART=y', 'CONFIG_LOG_BACKEND_UART_OUTPUT_TEXT=y',
                'CONFIG_LOG_BACKEND_SUPPORTS_FORMAT_TIMESTAMP=y'}),
                'Additional or unreviewed logging backend')
    else:
        require('CONFIG_PRINTK_SYNC=y' in config and 'CONFIG_LOG=y' not in config,
                'Required records are not synchronous direct printk')
    if 'CONFIG_SHELL=y' in config:
        require('# CONFIG_SHELL_ECHO_STATUS is not set' in config and
                'CONFIG_SHELL_PROMPT_UART=""' in config,
                'Shell echo/prompt could interleave required records')


def check_record_loss(rows):
    """A native logger loss report invalidates required physical observations."""
    require(not any('messages dropped' in line.lower() for _, line in rows),
            'Native logger reported lost records')
