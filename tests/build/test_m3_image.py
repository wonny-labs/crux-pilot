import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "target"))

"""Synthetic layout fixtures are never flashable test firmware."""
from pathlib import Path
import struct
import tempfile
import unittest
from m3_image import check_image


def fixture(root):
    binary = bytearray(0x2030)
    binary[:4] = b'FCFB'
    binary[0x1000:0x1004] = bytes.fromhex('d1002041')
    for offset, value in [(0x1004, 0x60002000), (0x1010, 0x60001020), (0x1014, 0x60001000)]:
        struct.pack_into('<I', binary, offset, value)
    elf = bytearray(0x24a0)
    struct.pack_into('<16sHHIIIIIHHHHHH', elf, 0,
                     b'\x7fELF\x01\x01' + bytes(10), 2, 40, 1, 0x60002001, 64, 0x2400, 0, 52, 32, 1, 40, 4, 2)
    struct.pack_into('<IIIIIIII', elf, 64, 1, 0x100, 0x60000000, 0x60000000, len(binary), len(binary), 5, 4)
    elf[0x100:0x100+len(binary)] = binary
    strings = b'\0rom_start\0.shstrtab\0.symtab\0work_stack\0z_main_stack\0'
    elf[0x2200:0x2200+len(strings)] = strings
    for index, name, address, size in [(1, b'work_stack', 0x80000040, 4160), (2, b'z_main_stack', 0x80002040, 2112)]:
        struct.pack_into('<IIIBBH', elf, 0x2300+16*index, strings.index(name), address, size, 1, 0, 1)
    for index, section in enumerate([(0,)*10,
            (strings.index(b'rom_start'), 1, 2, 0x60000000, 0x100, len(binary), 0, 0, 4, 0),
            (strings.index(b'.shstrtab'), 3, 0, 0, 0x2200, len(strings), 0, 0, 1, 0),
            (strings.index(b'.symtab'), 2, 0, 0, 0x2300, 48, 2, 0, 4, 16)]):
        struct.pack_into('<IIIIIIIIII', elf, 0x2400+index*40, *section)
    config = '''CONFIG_BOARD_REVISION="B"
CONFIG_BOARD_TARGET="mimxrt1060_evk@B/mimxrt1062/qspi"
CONFIG_FLASH_BASE_ADDRESS=0x60000000
CONFIG_FLASH_SIZE=8192
CONFIG_XIP=y
CONFIG_NXP_IMXRT_BOOT_HEADER=y
CONFIG_FLASH_LOAD_OFFSET=0
# CONFIG_BOOTLOADER_MCUBOOT is not set
CONFIG_HEAP_MEM_POOL_SIZE=0
CONFIG_MPU_STACK_GUARD=y
CONFIG_MAIN_STACK_SIZE=2048
# CONFIG_FPU is not set
CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE=32
CONFIG_CRUX_CONTROLLER_M3=y
CONFIG_WATCHDOG=y
CONFIG_HWINFO=y
CONFIG_GPIO=y
# CONFIG_NETWORKING is not set
CONFIG_CRUX_M3_TEST_COMMANDS=y
CONFIG_SHELL=y
# CONFIG_KERNEL_SHELL is not set
# CONFIG_DEVICE_SHELL is not set
# CONFIG_DATE_SHELL is not set
# CONFIG_DEVMEM_SHELL is not set
'''
    paths = [root / name for name in ('zephyr.bin', 'zephyr.elf', '.config')]
    paths[0].write_bytes(binary)
    paths[1].write_bytes(elf)
    paths[2].write_text(config)
    return paths


class ImageChecks(unittest.TestCase):
    def test_identified_layout_matches_elf(self):
        with tempfile.TemporaryDirectory() as d:
            image = fixture(Path(d))
            self.assertEqual(check_image(*image, 'probe')['main_stack']['bytes'], 2112)

    def test_tampered_bin_elf_header_and_configuration_are_rejected(self):
        for change in ['bin', 'base', 'boot', 'stack', 'network', 'missing-elf']:
            with self.subTest(change=change), tempfile.TemporaryDirectory() as d:
                binary, elf, config = fixture(Path(d))
                if change in ('bin', 'boot'):
                    data = bytearray(binary.read_bytes())
                    data[-1 if change == 'bin' else 0x1000] ^= 1
                    binary.write_bytes(data)
                elif change in ('base', 'stack'):
                    data = bytearray(elf.read_bytes())
                    struct.pack_into('<I', data, 64+12 if change == 'base' else 0x2300+16+8,
                                     0x60800000 if change == 'base' else 4096)
                    elf.write_bytes(data)
                elif change == 'network':
                    config.write_text(config.read_text().replace('# CONFIG_NETWORKING is not set', 'CONFIG_NETWORKING=y'))
                else:
                    elf.write_bytes(b'not an ELF')
                with self.assertRaises(ValueError):
                    check_image(binary, elf, config, 'probe')


if __name__ == '__main__':
    unittest.main()
