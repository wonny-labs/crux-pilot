"""Candidate native dispatch fence: no target/device access."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class RequestFence(unittest.TestCase):
    def test_native_width_owner_limits_and_erase_granules(self):
        # Removing the pre-dispatch bounds/overflow/erase checks must fail.
        source = r'''
#include "request_fence.h"
#include <assert.h>
int main(void) {
    assert(crux_pilot_flash_request_valid(0x620000, 1, false));
    assert(crux_pilot_flash_request_valid(0x7effff, 1, false));
    assert(crux_pilot_flash_request_valid(0x7f0000, 0x10000, false));
    assert(crux_pilot_flash_request_valid(0x7ff000, 4096, true));
    assert(crux_pilot_flash_request_valid(0x620000, 0x1d0000, true));
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
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / 'fence.c', Path(tmp) / 'fence'
            src.write_text(source)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall',
                            '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=2)


if __name__ == '__main__':
    unittest.main()
