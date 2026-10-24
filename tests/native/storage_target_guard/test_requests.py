"""Candidate native dispatch fence: no target/device access."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class RequestFence(unittest.TestCase):
    def test_native_width_partition_limits_and_erase_granules(self):
        # Removing the pre-dispatch bounds/overflow/erase checks must fail.
        source = r'''
#include "request_fence.h"
#include <assert.h>
int main(void) {
    assert(crux_pilot_flash_request_valid(0x620000, 1, false));
    assert(crux_pilot_flash_request_valid(0x7eefff, 1, false));
    assert(crux_pilot_flash_request_valid(0x7ef000, 4096, true));
    assert(crux_pilot_flash_request_valid(0x7effff, 1, false));
    assert(crux_pilot_flash_request_valid(0x7f0000, 0x10000, false));
    assert(crux_pilot_flash_request_valid(0x7ff000, 4096, true));
    assert(crux_pilot_flash_request_valid(0x620000, 0x1cf000, true));
    assert(!crux_pilot_flash_request_valid(0x620000, 0x1d0000, true));
    assert(!crux_pilot_flash_request_valid(0x7eefff, 2, false));
    assert(!crux_pilot_flash_request_valid(0x7ee000, 8192, true));
    assert(!crux_pilot_flash_request_valid(0x7ef000, 8192, true));
    assert(!crux_pilot_flash_request_valid(0, 4096, true));
    assert(!crux_pilot_flash_request_valid(0x1000, 1, false));
    assert(!crux_pilot_flash_request_valid(0x61ffff, 2, false));
    assert(!crux_pilot_flash_request_valid(0x620000, 0, false));
    assert(!crux_pilot_flash_request_valid(-1, 1, false));
    assert(!crux_pilot_flash_request_valid(INT64_MAX, 2, false));
    assert(!crux_pilot_flash_request_valid(0x7effff, 2, false));
    assert(!crux_pilot_flash_request_valid(0x800000, 1, false));
    assert(!crux_pilot_flash_request_valid(0x7f0000, SIZE_MAX, false));
    assert(!crux_pilot_flash_request_valid(0x620001, 4096, true));
    assert(!crux_pilot_flash_request_valid(0x620000, 4095, true));
    assert(!crux_pilot_flash_request_valid(0, 0x800000, true));
    return 0;
}
'''
        self.check_program(source)

    def test_declared_file_owner_cannot_address_scratch_or_nvs(self):
        # Omitting the owner constraint would permit each generic-valid request.
        source = r'''
#include "request_fence.h"
#include <assert.h>
int main(void) {
    assert(crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, 0x620000, 0x1cf000, true));
    assert(crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, 0x7eefff, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, 0x7ef000, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, 0x7eefff, 2, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, 0x7effff, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, 0x7f0000, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_SCRATCH, 0x7eefff, 1, false));
    assert(crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_SCRATCH, 0x7ef000, 4096, true));
    assert(crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_SCRATCH, 0x7effff, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_SCRATCH, 0x7effff, 2, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_SCRATCH, 0x7f0000, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_NVS, 0x7effff, 1, false));
    assert(crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_NVS, 0x7f0000, 0x10000, true));
    assert(crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_NVS, 0x7fffff, 1, false));
    assert(!crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_NVS, 0x7fffff, 2, false));
    for (int owner = CRUX_PILOT_FLASH_FILE; owner <= CRUX_PILOT_FLASH_NVS; ++owner) {
        assert(!crux_pilot_flash_owner_request_valid(owner, 0, 4096, true));
        assert(!crux_pilot_flash_owner_request_valid(owner, -1, 1, false));
        assert(!crux_pilot_flash_owner_request_valid(owner, INT64_MAX, 2, false));
        assert(!crux_pilot_flash_owner_request_valid(owner, 0x620000, SIZE_MAX, false));
        assert(!crux_pilot_flash_owner_request_valid(owner, 0x7ef000, 0, false));
        assert(!crux_pilot_flash_owner_request_valid(owner, 0x7ef001, 4096, true));
        assert(!crux_pilot_flash_owner_request_valid(owner, 0x7ef000, 4095, true));
    }
    assert(!crux_pilot_flash_owner_request_valid((enum crux_pilot_flash_owner)-1, 0x7ef000, 1, false));
    assert(!crux_pilot_flash_owner_request_valid((enum crux_pilot_flash_owner)99, 0x7ef000, 1, false));
    return 0;
}
'''
        self.check_program(source)

    def check_program(self, source):
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / 'fence.c', Path(tmp) / 'fence'
            src.write_text(source)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall',
                            '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(src), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, timeout=2)
            self.assertEqual(run.returncode, 0, run.stderr.decode())


if __name__ == '__main__':
    unittest.main()
