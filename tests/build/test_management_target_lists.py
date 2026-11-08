"""The RT1060 compile vehicle repeats the source lists of the native management composition.

tests/zephyr/management_rt1060_compile/management_target.cmake cannot include composition.cmake, services.cmake or
ssh.cmake, because they refuse a board on purpose. It repeats their component sources instead. This test fails
when a list drifts, in either direction, so that the vehicle keeps measuring the composition that the native image
runs. Logging is the one declared difference: no linked source uses crux/logging.h.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "tests/native/management_composition/composition"
VEHICLE = ROOT / "tests/zephyr/management_rt1060_compile"
TARGET = VEHICLE / "management_target.cmake"
SOURCE = re.compile(r'"\$\{(?:component|CRUX_NATIVE_MANAGEMENT_DIR|CRUX_MGMT_NATIVE_DIR)\}/([^"]+\.c)"')


def sources(path):
    return set(SOURCE.findall(path.read_text()))


class ManagementTargetLists(unittest.TestCase):
    def test_component_sources_match_the_native_composition(self):
        native = set()
        for name in ("composition.cmake", "services.cmake"):
            native |= sources(NATIVE / name)
        target = sources(TARGET)
        # Mbed profile only: the vehicle is on the Wolf profile, which has no core lock shim.
        native.discard("tf_psa_threading/crux_tf_psa_threading.c")
        # The two native files whose copies are in the vehicle's own src/ (see the next test).
        native -= {"crux_native_management.c", "crux_native_management_services.c"}
        self.assertEqual(sorted(native - target), [], "a native source is missing from the vehicle")
        self.assertEqual(sorted(target - native), [], "the vehicle lists a source the native image does not")

    def test_no_test_declaration_reaches_the_vehicle(self):
        text = TARGET.read_text()
        self.assertNotIn("CRUX_NATIVE_MANAGEMENT_TEST_IMAGE", text)
        self.assertIn("CONFIG_TEST_RANDOM_GENERATOR OR CONFIG_FAKE_ENTROPY_NATIVE_SIM", text)
        self.assertIn("CONFIG_ENTROPY_MCUX_TRNG", text)

    def test_vehicle_copies_are_the_native_files_with_only_the_declared_edits(self):
        # The vehicle compiles copies of two native files, because the declared test certificate's header
        # refuses a board. Each copy must equal its native file after the declared edits, and nothing else,
        # so that a change to the native composition's logic fails here until the copy follows it.
        start = (NATIVE / "crux_native_management.c").read_text()
        start = start.replace('#include "test_tls_credentials.h"\n', "")
        start = start.replace("#include <ff.h>\n", "#include <errno.h>\n#include <ff.h>\n", 1)
        start = start.replace('#include "crux_native_management.h"\n', HEAD_START, 1)
        cut = start.index("int crux_native_management_tls_test_credentials(int sec_tag)")
        start = start[:cut] + TLS_FUNCTION
        self.assertEqual(start, (VEHICLE / "src/mgmt_start.c").read_text())

        services = (NATIVE / "crux_native_management_services.c").read_text()
        services = services.replace('#include "crux_native_management_services.h"\n', HEAD_SERVICES, 1)
        services = services.replace("#include <zephyr/sys/util.h>\n",
                                    "#include <zephyr/sys/util.h>\n\n/* In mgmt_start.c. */\n"
                                    "int crux_mgmt_rt1060_tls_credentials(int sec_tag);\n", 1)
        services = services.replace("crux_native_management_tls_test_credentials(CONFIG_CRUX_WEB_TLS_SEC_TAG)",
                                    "crux_mgmt_rt1060_tls_credentials(CONFIG_CRUX_WEB_TLS_SEC_TAG)")
        services = services.replace('static const char *const dns_names[] = {"localhost"};',
                                    'static const char *const dns_names[] = {"crux-bench"};')
        services = services.replace("{{127, 0, 0, 1}}", "{{192, 168, 77, 2}}")
        services = services.replace('"crux native_sim test"', '"crux rt1060 bench"')
        self.assertEqual(services, (VEHICLE / "src/mgmt_services.c").read_text())


HEAD_START = """/* RT1060 compile vehicle: a copy of
 * tests/native/management_composition/composition/crux_native_management.c with one difference, the
 * TLS credentials. The native file includes the declared test certificate, whose header refuses any
 * build that is not native_sim, so this copy hands the generated certificate and the store's key to
 * the TLS credentials, as docs/tls-certificate.md step 6 says. Nothing else is changed. */
#include "crux_native_management.h"
#include <crux_tls_certificate_adapter.h>
"""
HEAD_SERVICES = """/* RT1060 compile vehicle: a copy of tests/native/management_composition/composition/
 * crux_native_management_services.c whose listener takes the generated certificate and the store's
 * key, in place of the declared test certificate. */
#include "crux_native_management_services.h"
"""
TLS_FUNCTION = """int crux_mgmt_rt1060_tls_credentials(int sec_tag)
{
    static crux_tls_certificate_material_t material;

    if (crux_tls_certificate_material_take(&material) != CRUX_TLS_CERTIFICATE_OK) {
        return -ENOENT;
    }
    int result = tls_credential_add(sec_tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                                    material.certificate, material.certificate_length);
    if (result == 0) {
        result = tls_credential_add(sec_tag, TLS_CREDENTIAL_PRIVATE_KEY, material.private_key,
                                    material.private_key_length);
    }
    return result;
}
"""


if __name__ == "__main__":
    unittest.main()
