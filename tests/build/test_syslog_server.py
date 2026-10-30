"""Host-level validation of Zephyr NET Syslog address settings."""
from pathlib import Path
import subprocess
import sys
import unittest

REPO = Path(__file__).resolve().parents[2]
VALIDATOR = REPO / 'components/logging/ports/zephyr/validate_syslog_server.py'


def run_validator(address, ipv4, ipv6):
    return subprocess.run(
        [sys.executable, str(VALIDATOR), address, str(int(ipv4)), str(int(ipv6))],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)


class SyslogServerValidationTest(unittest.TestCase):
    def test_accepts_zephyr_ipv4_and_ipv6_forms(self):
        accepted = [
            ('192.0.2.1', True, False),
            ('192.0.2.1:1', True, False),
            ('192.0.2.1:65535', True, False),
            ('[2001:db8::1]:15141', False, True),
            ('[2001:db8::2]', False, True),
            ('2001:db8::42', False, True),
            ('192.0.2.1:15141', True, True),
            ('[2001:db8::1]:15141', True, True),
        ]
        for address, ipv4, ipv6 in accepted:
            with self.subTest(address=address, ipv4=ipv4, ipv6=ipv6):
                result = run_validator(address, ipv4, ipv6)
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_rejects_invalid_address_family_syntax_and_port(self):
        rejected = [
            ('', True, False, 'numeric Syslog server address'),
            ('not-an-ip', True, False, 'IPv4 or IPv6 syntax'),
            ('tcp://192.0.2.1:15141', True, False, 'does not support TCP Syslog'),
            ('[127.0.0.1]:15141', True, False, 'bracketed Syslog addresses must use IPv6'),
            ('[2001:db8::zzz]:15141', False, True, 'IPv4 or IPv6 syntax'),
            ('[fe80::1%eth0]:15141', False, True, 'scoped IPv6 Syslog addresses are unsupported'),
            ('192.0.2.1:15141', False, True, 'address family does not match'),
            ('[2001:db8::1]:15141', True, False, 'address family does not match'),
            ('192.0.2.1:+15141', True, False, 'decimal UDP port'),
            ('192.0.2.1:0', True, False, 'UDP port 1..65535'),
            ('192.0.2.1:65536', True, False, 'UDP port 1..65535'),
            ('192.0.2.1:', True, False, 'decimal UDP port'),
            ('[2001:db8::1:15141', False, True, 'numeric Syslog server address'),
        ]
        for address, ipv4, ipv6, diagnostic in rejected:
            with self.subTest(address=address, ipv4=ipv4, ipv6=ipv6):
                result = run_validator(address, ipv4, ipv6)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(diagnostic, result.stdout)


if __name__ == '__main__':
    unittest.main()
