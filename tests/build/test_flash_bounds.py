"""Software-only negatives for the unadopted candidate Flash envelope."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

CHECKER = Path(__file__).resolve().parents[1] / 'native/flash_safety/image_bounds.py'
sys.path.insert(0, str(CHECKER.parent))
from image_bounds import check_image, check_requests, check_layout

BASE = 0x60000000
CEILING = 0x60620000


def fixture():
    """Small ELF32 ARM: Flash headers/code plus RAM VMA with Flash LMA."""
    binary = bytearray(0x2040)
    binary[:4] = b'FCFB'
    struct.pack_into('<8I', binary, 0x1000, 0x412000d1, BASE + 0x2000, 0,
                     BASE + 0x1030, BASE + 0x1020, BASE + 0x1000, 0, 0)
    struct.pack_into('<4I', binary, 0x1020, BASE, len(binary), 0, 0xffffffff)
    elf = bytearray(0x100 + len(binary))
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIIIIIHHHHHH', elf, 0, ident, 2, 40, 1,
                     BASE + 0x2001, 52, 0, 0, 52, 32, 2, 0, 0, 0)
    struct.pack_into('<8I', elf, 52, 1, 0x100, BASE, BASE, 0x2030, 0x2030, 5, 4)
    struct.pack_into('<8I', elf, 84, 1, 0x2130, 0x20000000,
                     BASE + 0x2030, 0x10, 0x30, 6, 4)
    elf[0x100:] = binary
    return elf, binary


class ImageBounds(unittest.TestCase):
    def test_valid_ram_vma_flash_lma(self):
        elf, binary = fixture()
        self.assertEqual(check_image(elf, binary)['image_bytes'], 0x2040)

    def test_exact_firmware_ceiling(self):
        elf, binary = fixture()
        binary.extend(bytes(0x620000 - len(binary)))
        struct.pack_into('<I', binary, 0x1024, len(binary))
        struct.pack_into('<I', elf, 84 + 12, CEILING - 0x10)
        check_image(elf, binary)

    def test_ram_vma_load_crosses_ceiling_independently_of_small_bin(self):
        elf, binary = fixture()
        struct.pack_into('<I', elf, 84 + 12, CEILING - 8)
        with self.assertRaisesRegex(ValueError, 'load.*firmware'):
            check_image(elf, binary)

    def test_load_in_storage_rejected(self):
        for address in (0x60620000, 0x607f0000, 0x60800000, 0x5fffffff, 0xfffffff8):
            with self.subTest(address=hex(address)):
                elf, binary = fixture()
                struct.pack_into('<I', elf, 84 + 12, address)
                with self.assertRaisesRegex(ValueError, 'load.*firmware'):
                    check_image(elf, binary)

    def test_oversized_bin_independently_of_valid_loads(self):
        elf, binary = fixture()
        binary.extend(bytes(0x620001 - len(binary)))
        with self.assertRaisesRegex(ValueError, 'BIN.*firmware'):
            check_image(elf, binary)

    def test_bin_span_must_match_all_loads(self):
        for adjustment in (-1, 1):
            elf, binary = fixture()
            binary = binary[:-1] if adjustment < 0 else binary + b'\0'
            with self.subTest(adjustment=adjustment), self.assertRaisesRegex(ValueError, 'span'):
                check_image(elf, binary)

    def test_malformed_elf_headers(self):
        for offset, value, fmt in ((0, 0, 'I'), (4, 2, 'B'), (5, 2, 'B'),
                                    (18, 62, 'H'), (40, 51, 'H'), (42, 31, 'H'),
                                    (28, 0xfffffff0, 'I'), (44, 0, 'H')):
            elf, binary = fixture()
            struct.pack_into('<' + fmt, elf, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                check_image(elf, binary)

    def test_truncated_elf_header_table_and_load(self):
        elf, binary = fixture()
        for length in (0, 51, 115, len(elf) - 1):
            with self.subTest(length=length), self.assertRaises(ValueError):
                check_image(elf[:length], binary)

    def test_file_size_larger_than_memory_rejected(self):
        elf, binary = fixture()
        struct.pack_into('<I', elf, 84 + 20, 0xf)
        with self.assertRaisesRegex(ValueError, 'memory'):
            check_image(elf, binary)

    def test_overlapping_loads_rejected(self):
        elf, binary = fixture()
        struct.pack_into('<I', elf, 84 + 12, BASE + 0x202f)
        with self.assertRaisesRegex(ValueError, 'overlap'):
            check_image(elf, binary)

    def test_non_file_backed_ram_load_is_ignored(self):
        elf, binary = fixture()
        struct.pack_into('<H', elf, 44, 3)
        struct.pack_into('<8I', elf, 116, 1, 0, 0x20000100, 0x20000100, 0, 64, 6, 4)
        check_image(elf, binary)

    def test_boot_header_fields_independently_rejected(self):
        # Literal offsets cover FCB, IVT tag/entry/pointers, boot base/length/plugin.
        for offset, value in ((0, 0), (0x1000, 0), (0x1004, BASE + 0x3000),
                              (0x1010, BASE + 0x1024), (0x1014, BASE + 0x1004),
                              (0x1020, BASE + 4), (0x1024, 0x2041), (0x1028, 1)):
            elf, binary = fixture()
            struct.pack_into('<I', binary, offset, value)
            with self.subTest(offset=hex(offset)), self.assertRaisesRegex(ValueError, 'boot|FCB|IVT'):
                check_image(elf, binary)

    def test_truncated_boot_data(self):
        elf, binary = fixture()
        binary = binary[:0x1028]
        # Keep span valid so this exercises the header bound itself.
        struct.pack_into('<H', elf, 44, 1)
        struct.pack_into('<I', elf, 52 + 16, len(binary))
        with self.assertRaisesRegex(ValueError, 'boot'):
            check_image(elf, binary)

    def test_layout_overlap_and_alignment(self):
        check_layout()
        for layout in (
                {'firmware': (BASE, 0x60621000), 'littlefs': (CEILING, 0x607f0000), 'nvs': (0x607f0000, 0x60800000)},
                {'firmware': (BASE, CEILING), 'littlefs': (CEILING + 1, 0x607f0000), 'nvs': (0x607f0000, 0x60800000)}):
            with self.subTest(layout=layout), self.assertRaises(ValueError):
                check_layout(layout)


class Requests(unittest.TestCase):
    def test_valid_owner_edges_and_byte_write_granule(self):
        check_requests([('write', 'littlefs', CEILING, 1),
                        ('erase', 'littlefs', 0x607ef000, 4096),
                        ('write', 'nvs', 0x607f0000, 1),
                        ('erase', 'nvs', 0x607ff000, 4096)])

    def test_firmware_and_boot_headers_protected(self):
        for operation in ('write', 'erase'):
            for address in (BASE, BASE + 0x1000, BASE + 0x2000, CEILING - 4096):
                with self.subTest(operation=operation, address=address), self.assertRaises(ValueError):
                    check_requests([(operation, 'littlefs', address, 4096)])

    def test_owner_crossing_and_device_bounds_rejected(self):
        for owner, address, size in (('littlefs', 0x607effff, 2),
                                     ('nvs', 0x607effff, 2), ('nvs', 0x60800000, 1),
                                     ('littlefs', -1, 1), ('nvs', 0xffffffff, 2),
                                     ('firmware', BASE, 1), ('unknown', CEILING, 1)):
            with self.subTest(owner=owner, address=address), self.assertRaises(ValueError):
                check_requests([('write', owner, address, size)])

    def test_erase_alignment_and_invalid_lengths(self):
        for operation, address, size in (('erase', CEILING + 1, 4096),
                                         ('erase', CEILING, 4095), ('write', CEILING, 0),
                                         ('write', CEILING, -1), ('read', CEILING, 1)):
            with self.subTest(operation=operation, address=address, size=size), self.assertRaises(ValueError):
                check_requests([(operation, 'littlefs', address, size)])

    def test_overlapping_request_batch_rejected(self):
        with self.assertRaisesRegex(ValueError, 'overlap'):
            check_requests([('erase', 'littlefs', CEILING, 4096),
                            ('write', 'littlefs', CEILING + 1, 1)])

    def test_adjacent_requests_allowed(self):
        check_requests([('erase', 'littlefs', CEILING, 4096),
                        ('erase', 'littlefs', CEILING + 4096, 4096)])


class CommandLine(unittest.TestCase):
    def test_cli_reads_files_and_returns_failure_for_invalid_image(self):
        with tempfile.TemporaryDirectory() as directory:
            elf, binary = fixture()
            ep, bp = Path(directory) / 'image.elf', Path(directory) / 'image.bin'
            ep.write_bytes(elf)
            bp.write_bytes(binary)
            command = [sys.executable, str(CHECKER), '--elf', str(ep), '--bin', str(bp)]
            valid = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(valid.returncode, 0, valid.stderr)
            self.assertIn('candidate', valid.stdout)
            bp.write_bytes(b'')
            invalid = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(invalid.returncode, 0)
            self.assertIn('BIN', invalid.stderr)


if __name__ == '__main__':
    unittest.main()
