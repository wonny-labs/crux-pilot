"""The declared TLS profile in the web adapter's source, held by scripts/check-web-boundary.py (Issue #185).

A Mbed TLS run reads NET_IPPROTO_TLS_1_2 as a minimum and still negotiates TLS 1.3, so losing the listener's
version or suite list would not show on the wire there. The check reads the source instead.
"""
import importlib.util
from pathlib import Path
import unittest

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("web_boundary_under_test", REPO / "scripts/check-web-boundary.py")
boundary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(boundary)
ADAPTER = (REPO / "components/web/ports/zephyr/web_http_server.c").read_text(encoding="utf-8")


class TlsProfile(unittest.TestCase):
    def errors(self, source):
        return boundary.tls_profile_errors(boundary.COMMENT.sub("", source))

    def test_the_adapter_holds_the_profile(self):
        self.assertEqual(self.errors(ADAPTER), [])

    def test_another_version_is_refused(self):
        changed = ADAPTER.replace("NET_IPPROTO_TLS_1_3", "NET_IPPROTO_TLS_1_2")
        self.assertNotEqual(changed, ADAPTER)
        self.assertTrue(any("TLS 1.3" in error for error in self.errors(changed)))

    def test_the_servers_protocol_is_refused(self):
        changed = ADAPTER.replace("NET_SOCK_STREAM, NET_IPPROTO_TLS_1_3", "NET_SOCK_STREAM, protocol")
        self.assertNotEqual(changed, ADAPTER)
        self.assertTrue(self.errors(changed))

    def test_another_suite_is_refused(self):
        changed = ADAPTER.replace("{0x1301}", "{0x1302}")
        self.assertNotEqual(changed, ADAPTER)
        self.assertTrue(any("0x1301" in error for error in self.errors(changed)))

    def test_a_second_suite_is_refused(self):
        changed = ADAPTER.replace("{0x1301}", "{0x1301, 0x1303}")
        self.assertTrue(self.errors(changed))

    def test_a_missing_suite_option_is_refused(self):
        changed = ADAPTER.replace("ZSOCK_TLS_CIPHERSUITE_LIST", "ZSOCK_TLS_ALPN_LIST")
        self.assertTrue(self.errors(changed))

    def test_a_missing_hook_is_refused(self):
        changed = ADAPTER.replace("{.socket_create =", "{.unused =")
        self.assertNotEqual(changed, ADAPTER)
        self.assertTrue(self.errors(changed))


if __name__ == "__main__":
    unittest.main()
