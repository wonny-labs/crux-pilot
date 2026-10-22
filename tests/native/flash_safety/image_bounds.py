"""Read-only bounds oracle for the *unadopted* standalone RT1060 candidate.

This test helper checks ELF32 ARM file-backed PT_LOAD physical addresses (including
RAM VMAs with Flash LMAs), BIN span and selected pinned ROM headers. It does not
compare ELF/BIN contents, implement a general ELF security parser, restrict a
programmer, or enforce production driver/linker protection. Request batches are
hypothetical disjoint extents, not sequences of real erase/program commands.

Layout: docs/storage-configuration-assessment.md. Header size semantics: pinned
Zephyr soc/nxp/imxrt/imxrt10xx/soc.c uses _flash_used for XIP boot_data.size.
No programming, erase, reset or probe APIs are imported or invoked.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

BASE = 0x60000000
END = 0x60800000
SECTOR = 4096
CANDIDATE = {
    'firmware': (BASE, 0x60620000),
    'littlefs': (0x60620000, 0x607f0000),
    'nvs': (0x607f0000, END),
}
ELF_HEADER = struct.Struct('<16sHHIIIIIHHHHHH')
PROGRAM_HEADER = struct.Struct('<8I')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_layout(layout=None):
    """Validate candidate geometry and disjoint exclusive ownership."""
    layout = CANDIDATE if layout is None else layout
    require(set(layout) == {'firmware', 'littlefs', 'nvs'}, 'invalid layout owners')
    ordered = []
    for owner, (start, end) in layout.items():
        require(BASE <= start < end <= END, 'layout outside Flash')
        require(start % SECTOR == 0 and end % SECTOR == 0, 'unaligned layout')
        ordered.append((start, end, owner))
    ordered.sort()
    require(ordered[0][0] == BASE and ordered[-1][1] == END, 'layout span mismatch')
    for previous, current in zip(ordered, ordered[1:]):
        require(previous[1] == current[0], 'layout overlap or gap')
    require(ordered[0][2] == 'firmware', 'firmware must own boot headers')


def disjoint(extents, message):
    ordered = sorted(extents)
    for previous, current in zip(ordered, ordered[1:]):
        require(previous[1] <= current[0], message)


def flash_loads(elf):
    """Parse only the ELF header and PT_LOAD table needed for the bounds check."""
    require(len(elf) >= ELF_HEADER.size, 'truncated ELF header')
    header = ELF_HEADER.unpack_from(elf)
    ident, kind, machine, version = header[:4]
    require(ident[:7] == b'\x7fELF\x01\x01\x01', 'expected little-endian ELF32 version 1')
    require(kind == 2 and machine == 40 and version == 1, 'expected ARM executable ELF')
    table, header_size, stride, count = header[5], header[8], header[9], header[10]
    require(header_size == ELF_HEADER.size and stride == PROGRAM_HEADER.size,
            'unexpected ELF header layout')
    require(count > 0 and table >= header_size and table + count * stride <= len(elf),
            'missing or truncated ELF program headers')
    loads = []
    for index in range(count):
        kind, offset, vma, physical, size, memory, flags, align = PROGRAM_HEADER.unpack_from(
            elf, table + index * stride)
        if kind != 1:
            continue
        require(memory >= size, 'ELF load file size exceeds memory size')
        if not size:
            continue
        require(BASE <= physical < physical + size <= CANDIDATE['firmware'][1],
                'ELF load outside candidate firmware envelope')
        require(offset + size <= len(elf), 'truncated ELF load contents')
        loads.append((physical, physical + size))
    require(loads and min(start for start, _ in loads) == BASE, 'missing firmware base load')
    disjoint(loads, 'ELF load overlap')
    return loads


def check_boot(binary, loads):
    require(len(binary) >= 0x1030, 'truncated boot data')
    require(binary[:4] == b'FCFB', 'invalid FCB tag')
    ivt = struct.unpack_from('<8I', binary, 0x1000)
    require(ivt[0] == 0x412000d1, 'invalid IVT header')
    require(ivt[1] == BASE + 0x2000 and
            any(start <= ivt[1] < end for start, end in loads), 'invalid IVT entry')
    require(ivt[4] == BASE + 0x1020 and ivt[5] == BASE + 0x1000,
            'invalid IVT boot-data/self pointers')
    boot_start, boot_size, plugin, placeholder = struct.unpack_from('<4I', binary, 0x1020)
    require(boot_start == BASE and boot_size == len(binary) and plugin == 0,
            'invalid boot data start/length/plugin')


def check_image(elf, binary):
    """Check bytes without accessing or modifying any target or input file."""
    check_layout()
    require(0 < len(binary) <= CANDIDATE['firmware'][1] - BASE,
            'BIN outside candidate firmware envelope')
    loads = flash_loads(elf)
    require(max(end for _, end in loads) - BASE == len(binary), 'ELF/BIN image span mismatch')
    check_boot(binary, loads)
    return {'status': 'candidate bounds only', 'image_bytes': len(binary),
            'firmware_limit': CANDIDATE['firmware'][1], 'file_backed_flash_loads': loads}


def check_requests(requests):
    """Reject hypothetical storage extents outside exclusive candidate ownership.

    Each tuple is (operation, owner, absolute CPU address, byte count). Writes
    use the pinned native API's one-byte granule; erases require 4 KiB sectors.
    Firmware, including FCB/IVT/boot data, is never a storage request owner.
    """
    check_layout()
    extents = []
    for operation, owner, address, size in requests:
        require(operation in ('write', 'erase'), 'invalid request operation')
        require(owner in ('littlefs', 'nvs'), 'invalid storage owner; firmware protected')
        start, end = CANDIDATE[owner]
        require(size > 0 and start <= address < address + size <= end,
                'request outside owner region or invalid size')
        if operation == 'erase':
            require(address % SECTOR == 0 and size % SECTOR == 0, 'unaligned erase request')
        extents.append((address, address + size))
    disjoint(extents, 'request overlap')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--bin', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = check_image(args.elf.read_bytes(), args.bin.read_bytes())
    except (OSError, ValueError) as error:
        print(f'candidate bounds rejected: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
