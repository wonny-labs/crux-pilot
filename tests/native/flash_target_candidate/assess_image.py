"""Fail-closed phase assessment of this candidate; not a universal closure verifier.

Exit 2 means mandatory closure obligations remain. Symbol measurements establish
only the named placements and retain concrete exception blockers in the final image.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def symbols(data):
    if data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected ELF32 little-endian version 1')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if header[2] != 40 or header[11] != 40:
        raise ValueError('expected ARM section layout')
    sections = [struct.unpack_from('<10I', data, header[6] + i * header[11])
                for i in range(header[12])]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], 16):
            name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
            result[strings[name:].split(b'\0', 1)[0].decode()] = value
    return result


def vector_words(data, address):
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    for i in range(header[10]):
        kind, offset, vma, physical, size, memory, flags, align = struct.unpack_from(
            '<8I', data, header[5] + i * header[9])
        if kind == 1 and vma <= address and address + 16 <= vma + size:
            values = struct.unpack_from('<4I', data, offset + address - vma)
            return dict(zip(['initial_sp', 'reset', 'nmi', 'hard_fault'], map(hex, values)))
    raise ValueError('Flash vector source is not file-backed')


def assess(data, config):
    table = symbols(data)
    required = ['crux_flash_poc_terminal', 'flash_flexspi_nor_write',
                'flash_flexspi_nor_erase', 'flash_flexspi_nor_init',
                'memc_flexspi_transfer', 'FLEXSPI_TransferBlocking',
                'FLEXSPI_SoftwareReset', 'memc_flexspi_config_0',
                '_vector_start', '_sram_vector_start', 'z_arm_nmi', 'z_arm_fault',
                'flash_flexspi_nor_base_lut', 'flexspi_probe_lut', 'nor_write_buf',
                'flash_flexspi_nor_data_0', 'memc_flexspi_data_0',
                'z_main_stack', 'z_interrupt_stacks', 'flash_flexspi_nor_api', 'memcpy']
    missing = [name for name in required if name not in table]
    blockers = [
        'Full startup/copy/clock/probe code and pointer closure is unproved; probe can program NOR status/configuration before main.',
        'Complete hazardous transitive code/data/indirect/veneer paths, stack and cache restoration are unproved.',
        'RAM vector copying does not relocate NMI/fault handlers and their callees, data or stacks.',
        'Disabled driver selections do not establish peripheral/bus-master quiescence or bootloader ownership.',
        'Observation budgets are not elapsed-time deadlines; MMIO transactions can stall and part-specific limits are unknown.',
        'Watchdog hardware state, reset while NOR busy, power-loss and uncertain-state recovery policy remain unqualified.',
    ]
    if missing:
        blockers.append('Missing measurement roots: ' + ', '.join(missing))
    flash_handlers = {name: hex(table[name]) for name in ['z_arm_nmi', 'z_arm_fault']
                      if name in table and 0x60000000 <= (table[name] & ~1) < 0x60800000}
    relevant_config = [line for line in config.splitlines() if any(
        line.startswith('CONFIG_' + key + '=') or line == '# CONFIG_' + key + ' is not set'
        for key in ['XIP', 'SRAM_VECTOR_TABLE', 'DMA', 'ETH_DRIVER', 'NETWORKING',
                    'WATCHDOG', 'USB_DEVICE_STACK', 'FLASH', 'FPU', 'MAIN_STACK_SIZE'])]
    return {
        'status': 'NOT_READY_FOR_PHYSICAL_WRITE',
        'elf_sha256': hashlib.sha256(data).hexdigest(),
        'scope': 'Named final-image measurements plus explicit unresolved phase obligations; no complete closure PASS.',
        'selected_addresses': {name: hex(table[name]) for name in required if name in table},
        'flash_exception_handlers': flash_handlers,
        'vector_source_words': vector_words(data, table['_vector_start']),
        'configuration': relevant_config,
        'phase_assessment': {
            'startup_probe': 'BLOCKED', 'normal_idle_read': 'UNQUALIFIED',
            'entry_preparation': 'BLOCKED', 'program_erase_busy': 'BLOCKED',
            'successful_completion': 'BLOCKED', 'failure_terminal': 'BLOCKED',
            'interrupts_exceptions': 'BLOCKED', 'bus_masters_watchdog': 'BLOCKED'},
        'blockers': blockers,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = assess(args.elf.read_bytes(), args.config.read_text())
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
