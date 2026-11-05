"""The check on a management image must fail on one that links a fake or the wrong provider."""
import re
import runpy
import unittest
from pathlib import Path

CHECK = runpy.run_path(str(Path(__file__).resolve().parents[1].parent / "scripts/check-composition-image.py"))
PORTS = CHECK["PORTS"]
read_symbols = CHECK["read_symbols"]
errors_in = CHECK["errors_in"]


ROOT = "/ws/crux-pilot"


def good_image(profile="mbed"):
    lines = [f"0000000000400000 t {port}\t{ROOT}/{source}:10" for port, source in PORTS.items()]
    if profile == "mbed":
        lines.append("0000000000400000 T psa_crypto_init\t/ws/modules/crypto/tf-psa-crypto/core/psa_crypto.c:1")
        lines.append("0000000000400000 T mbedtls_ssl_setup\t/ws/modules/crypto/mbedtls/library/ssl_tls.c:1")
    else:
        lines.append("0000000000400000 T psa_crypto_init\t/ws/modules/crypto/wolfpsa/src/psa_engine.c:1")
        lines.append("0000000000400000 T wolfSSL_new\t/ws/modules/crypto/wolfssl/src/ssl.c:1")
    return lines


def errors(lines, profile="mbed"):
    return errors_in(read_symbols("\n".join(lines)), profile, ROOT)


class CompositionImageTest(unittest.TestCase):
    def test_a_good_image_passes(self):
        self.assertEqual(errors(good_image()), [])
        self.assertEqual(errors(good_image("wolf"), "wolf"), [])

    def test_a_port_from_a_fake_fails(self):
        lines = [line.replace(f"{ROOT}/components/access/ports/psa/access_psa.c",
                              f"{ROOT}/tests/access/fake_ports.c") for line in good_image()]
        found = errors(lines)
        self.assertTrue(any("crux_access_port_derive: comes from" in error for error in found), found)
        self.assertTrue(any("comes from test code" in error for error in found), found)

    def test_a_fake_under_the_tests_with_the_real_path_as_its_suffix_fails(self):
        moved = "/ws/crux-pilot/tests/native/fake/components/access/ports/psa/access_psa.c"
        lines = [line.replace("/ws/crux-pilot/components/access/ports/psa/access_psa.c", moved)
                 for line in good_image()]
        self.assertTrue(any("crux_access_port_derive: comes from" in error for error in errors(lines)))

    def test_psa_from_a_stub_fails(self):
        for stub in ("tests/psa_crypto/stubs/stub.c", "tests/psa_ports/device_id_double.c",
                     "tests/management_providers/events.c", "tests/native/other/main.c"):
            lines = [re.sub(r"\t/ws/modules/crypto/tf-psa-crypto/.*", f"\t{ROOT}/{stub}:1", line)
                     for line in good_image()]
            found = errors(lines)
            self.assertTrue(any("psa_crypto_init: comes from test code" in error for error in found), found)
            self.assertTrue(any("must come from a path with" in error for error in found), found)

    def test_the_composition_may_hold_its_own_code(self):
        lines = good_image() + [f"0000000000400000 T test_main\t{ROOT}/tests/native/management_composition/src/main.c:1"]
        self.assertEqual(errors(lines), [])

    def test_every_status_port_and_time_are_held(self):
        for name in ("crux_mgmt_status_port_uptime_ms", "crux_mgmt_status_port_clock",
                     "crux_mgmt_status_port_system", "crux_mgmt_status_port_net", "crux_monotonic_now"):
            self.assertIn(name, PORTS)

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
        lines = good_image() + [f"0000000000400000 t crux_access_port_random\t{ROOT}/components/access/ports/psa/access_psa.c:9"]
        self.assertIn("crux_access_port_random: 2 definitions", errors(lines))

    def test_the_other_profile_fails(self):
        self.assertTrue(errors(good_image("wolf"), "mbed"))
        self.assertTrue(errors(good_image() + ["0000000000400000 T wolfSSL_new\t/x/ssl.c:1"], "mbed"))


if __name__ == "__main__":
    unittest.main()
