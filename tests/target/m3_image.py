"""Pure ELF/BIN/config checks for the identified EVKB health images."""
import struct
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_image(binary_path, elf_path, config_path, profile):
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
        loads.append(dict(offset=offset, address=address, bytes=size))
    require(loads and min(s['address'] for s in loads) == 0x60000000, 'Wrong firmware base')
    require(max(s['address'] + s['bytes'] for s in loads) - 0x60000000 == len(binary), 'BIN span mismatch')
    for s in loads:
        o = s['address'] - 0x60000000
        require(binary[o:o+s['bytes']] == elf[s['offset']:s['offset']+s['bytes']], 'ELF/BIN data mismatch')
    sections = [struct.unpack_from('<IIIIIIIIII', elf, h[6] + i * h[11]) for i in range(h[12])]
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
    require(symbols.get('work_stack', {}).get('bytes') == 4160, 'Unexpected worker stack object')
    require(symbols.get('z_main_stack', {}).get('bytes') == 2112, 'Unexpected main stack object')
    config = Path(config_path).read_text().splitlines()
    required = ['CONFIG_BOARD_REVISION="B"', 'CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"',
                'CONFIG_FLASH_BASE_ADDRESS=0x60000000', 'CONFIG_FLASH_SIZE=8192', 'CONFIG_XIP=y',
                'CONFIG_NXP_IMXRT_BOOT_HEADER=y', 'CONFIG_FLASH_LOAD_OFFSET=0',
                '# CONFIG_BOOTLOADER_MCUBOOT is not set', 'CONFIG_HEAP_MEM_POOL_SIZE=0', 'CONFIG_MPU_STACK_GUARD=y',
                'CONFIG_MAIN_STACK_SIZE=2048', '# CONFIG_FPU is not set',
                'CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE=32', 'CONFIG_CRUX_CONTROLLER_M3=y', 'CONFIG_WATCHDOG=y',
                'CONFIG_HWINFO=y', 'CONFIG_GPIO=y', '# CONFIG_NETWORKING is not set']
    require(profile in ('probe', 'normal'), 'Wrong profile')
    required += ['CONFIG_CRUX_M3_TEST_COMMANDS=y', 'CONFIG_SHELL=y'] if profile == 'probe' else ['# CONFIG_SHELL is not set']
    if profile == 'probe':
        required += [f'# CONFIG_{module}_SHELL is not set' for module in ('KERNEL', 'DEVICE', 'DATE', 'DEVMEM')]
    for item in required:
        require(item in config, f'Unexpected configuration: {item}')
    return dict(elf_segments_match_bin=True, bin_address=0x60000000, bin_bytes=len(binary),
                elf_load_segments=loads, worker_stack=symbols['work_stack'],
                main_stack=symbols.get('z_main_stack'))
