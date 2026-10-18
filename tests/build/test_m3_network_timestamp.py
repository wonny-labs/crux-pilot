"""Exercise production timestamp reporting with a clock advancing on every read."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cc'), 'C compiler required')
class NetworkTimestampTests(unittest.TestCase):
    def test_exchange_uses_one_completion_snapshot(self):
        source = (ROOT / 'samples/controller/zephyr/network_client.c').read_text()
        body = source[source.index('static int perform_exchange('):
                      source.index('\nint controller_network_pair(')]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
static int64_t now, expected_start;
static unsigned int records, owned_sockets;
static int cleanup_uncertain;
static int64_t k_uptime_get(void) { return now++; }
static int exchange(unsigned int p, unsigned int t, unsigned int s, int64_t d) {
    (void)p; (void)t; (void)s; (void)d;
    expected_start = now - 1;
    now += 17;
    return 0;
}
static void controller_network_report(void) {}
static int settle_contexts(int64_t deadline) { (void)deadline; return 0; }
static int printk(const char *format, ...) {
    (void)format;
    va_list args;
    va_start(args, format);
    for (int i = 0; i < 3; ++i) (void)va_arg(args, unsigned int);
    assert(va_arg(args, int) == 0);
    long long ended = va_arg(args, long long);
    long long duration = va_arg(args, long long);
    va_end(args);
    assert(ended == now - 1);
    assert(ended - duration == expected_start);
    ++records;
    return 0;
}
'''
        harness += body + r'''
int main(void) {
    now = INT64_C(4294967300);
    assert(perform_exchange(1, 1, 1, now + 2000) == 0);
    assert(perform_exchange(2, 1, 1, now + 2000) == 0);
    assert(records == 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder)
            (path / 'check.c').write_text(harness)
            compiled = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                       str(path / 'check.c'), '-o', str(path / 'check')],
                                      capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(path / 'check')], check=True, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
