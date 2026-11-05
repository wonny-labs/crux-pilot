"""The check on a management image must fail on one that links a fake or the wrong provider."""
import runpy
import unittest
from pathlib import Path

CHECK = runpy.run_path(str(Path(__file__).resolve().parents[1].parent / "scripts/check-composition-image.py"))
PORTS = CHECK["PORTS"]
read_symbols = CHECK["read_symbols"]
errors_in = CHECK["errors_in"]


def good_image(profile="mbed"):
    lines = [f"0000000000400000 t {port}\t/ws/crux-pilot/{source}:10" for port, source in PORTS.items()]
    lines.append("0000000000400000 T psa_crypto_init\t/ws/modules/crypto/tf-psa-crypto/core/psa_crypto.c:1")
    if profile == "mbed":
        lines.append("0000000000400000 T mbedtls_ssl_setup\t/ws/modules/crypto/mbedtls/library/ssl_tls.c:1")
    else:
        lines.append("0000000000400000 T wolfSSL_new\t/ws/modules/crypto/wolfssl/src/ssl.c:1")
    return lines


def errors(lines, profile="mbed"):
    return errors_in(read_symbols("\n".join(lines)), profile)


class CompositionImageTest(unittest.TestCase):
    def test_a_good_image_passes(self):
        self.assertEqual(errors(good_image()), [])
        self.assertEqual(errors(good_image("wolf"), "wolf"), [])

    def test_a_port_from_a_fake_fails(self):
        lines = [line.replace("/ws/crux-pilot/components/access/ports/psa/access_psa.c",
                              "/ws/crux-pilot/tests/access/fake_ports.c") for line in good_image()]
        found = errors(lines)
        self.assertTrue(any("crux_access_port_derive: comes from" in error for error in found), found)
        self.assertTrue(any("comes from test code" in error for error in found), found)

    def test_a_fake_symbol_fails(self):
        for name in ("access_fake_reset", "credential_fake_declare", "security_event_fake_boot",
                     "crux_access_test_reset", "crux_credential_test_reset", "crux_file_fake_cut"):
            found = errors(good_image() + [f"0000000000400000 T {name}\t/somewhere/else.c:1"])
            self.assertTrue(any(name in error for error in found), name)

    def test_a_missing_port_fails(self):
        lines = [line for line in good_image() if " crux_credential_port_seal\t" not in line]
        self.assertIn("crux_credential_port_seal: no provider in the image", errors(lines))

    def test_a_port_with_no_line_information_proves_nothing(self):
        lines = [line.split("\t")[0] if " crux_access_port_hmac\t" in line else line for line in good_image()]
        self.assertTrue(any("crux_access_port_hmac: comes from None" in error for error in errors(lines)))

    def test_two_definitions_fail(self):
        lines = good_image() + ["0000000000400000 t crux_access_port_random\t/x/components/access/ports/psa/access_psa.c:9"]
        self.assertIn("crux_access_port_random: 2 definitions", errors(lines))

    def test_the_other_profile_fails(self):
        self.assertTrue(errors(good_image("wolf"), "mbed"))
        self.assertTrue(errors(good_image() + ["0000000000400000 T wolfSSL_new\t/x/ssl.c:1"], "mbed"))


if __name__ == "__main__":
    unittest.main()
