"""The check on a management image must fail on one that links a fake or the wrong provider.

The services image of Issue #242 adds mutations of its wiring: a missing service, a port from a fake, the
wrong PSA provider, a test random generator, and a route table that differs from what the components define.
"""
import re
import runpy
import unittest
from pathlib import Path

CHECK = runpy.run_path(str(Path(__file__).resolve().parents[1].parent / "scripts/check-composition-image.py"))
PORTS = CHECK["PORTS"]
read_symbols = CHECK["read_symbols"]
errors_in = CHECK["errors_in"]
SERVICE_PORTS = CHECK["SERVICE_PORTS"]
SERVICES = CHECK["SERVICES"]
REPOSITORY = Path(__file__).resolve().parents[2]


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


def services_image(profile="mbed"):
    lines = good_image(profile)
    for table in (SERVICE_PORTS, SERVICES):
        lines += [f"0000000000400000 T {name}\t{ROOT}/{source}:10" for name, source in table.items()]
    lines += [f"0000000000400000 R {name}\t{source}:1" for name, source in CHECK["expected_routes"](ROOT).items()]
    generated = f"{ROOT}/build/x/crux_web_assets/app/crux_web_assets.c"
    lines.append(f"0000000000400000 R crux_web_route_crux_web_asset_route_0\t{generated}:5")
    lines += [f"0000000000400000 R {name}\t{ROOT}/build/x/crux_web_generated/app/crux_web_routes.c:3"
              for name in CHECK["ROUTE_TABLE_SYMBOLS"]]
    lines.append(f"0000000000400000 t entropy_native_sim_get_entropy\t/ws/zephyr/drivers/entropy/fake_entropy_native_sim.c:32")
    lines.append("0000000000400000 T z_impl_sys_rand_get\t/ws/zephyr/subsys/random/random_entropy_device.c:61")
    if profile == "mbed":
        lines.append("0000000000400000 t mbedtls_psa_external_get_random\t/ws/zephyr/modules/mbedtls/zephyr_entropy.c:59")
    lines.append(f"0000000000400000 T main\t{ROOT}/tests/native/management_services/src/main.c:1")
    # The core's functions share the routes' prefix, and are not routes.
    lines.append(f"0000000000400000 T crux_web_route_find\t{ROOT}/components/web/src/web_route.c:20")
    return lines


def service_errors(lines, profile="mbed"):
    return errors_in(read_symbols("\n".join(lines)), profile, ROOT, services=True)


class ServicesImageTest(unittest.TestCase):
    def test_a_good_services_image_passes(self):
        self.assertEqual(service_errors(services_image()), [])
        self.assertEqual(service_errors(services_image("wolf"), "wolf"), [])

    def test_the_base_check_refuses_the_services_image_source(self):
        found = errors(services_image())
        self.assertTrue(any("comes from test code" in error for error in found), found)

    def test_a_missing_service_fails(self):
        for name in SERVICES:
            lines = [line for line in services_image() if f" {name}\t" not in line]
            self.assertIn(f"{name}: not in the image, so its service or provider is missing",
                          service_errors(lines), name)

    def test_every_listed_service_is_named(self):
        for name in ("crux_web_session_init", "crux_web_management_init", "crux_network_settings_start",
                     "crux_tls_certificate_start", "crux_cli_init", "crux_file_service_start",
                     "crux_config_service_start", "crux_mgmt_status_start", "crux_security_event_init"):
            self.assertIn(name, SERVICES)

    def test_a_service_port_from_a_fake_fails(self):
        fakes = {"crux_cli_port_event": "tests/cli/fake_events.c",
                 "crux_network_settings_port_apply": "tests/network_settings/fake_port.c",
                 "crux_tls_certificate_port_sign": "tests/tls_certificate/fake_ports.c",
                 "crux_web_session_port_random": "tests/web/session/session_test.c"}
        for name, fake in fakes.items():
            lines = [line.replace(f"{ROOT}/{SERVICE_PORTS[name]}", f"{ROOT}/{fake}") if f" {name}\t" in line
                     else line for line in services_image()]
            found = service_errors(lines)
            self.assertTrue(any(f"{name}: comes from" in error for error in found), found)
            self.assertTrue(any("comes from test code" in error for error in found), found)

    def test_the_wrong_psa_provider_fails(self):
        lines = [re.sub(r"\t/ws/modules/crypto/tf-psa-crypto/.*", "\t/ws/modules/crypto/wolfpsa/src/psa_engine.c:1",
                        line) for line in services_image()]
        self.assertTrue(any("psa_crypto_init: comes from" in error for error in service_errors(lines)))
        self.assertTrue(service_errors(services_image("mbed"), "wolf"))

    def test_a_test_random_generator_fails(self):
        for source in ("random_xoshiro128.c", "random_timer.c", "random_test_csprng.c"):
            lines = [line.replace("random_entropy_device.c", source) for line in services_image()]
            found = service_errors(lines)
            self.assertTrue(any("a random source other than the entropy driver" in error for error in found), found)
        lines = [line for line in services_image() if "mbedtls_psa_external_get_random" not in line]
        self.assertTrue(any("mbedtls_psa_external_get_random: must come from" in error
                            for error in service_errors(lines)))
        lines = [line for line in services_image() if "entropy_native_sim_get_entropy" not in line]
        self.assertTrue(any("entropy_native_sim_get_entropy: must come from" in error
                            for error in service_errors(lines)))

    def test_an_added_route_fails(self):
        for source in ("tests/native/web_http/src/probe_routes.c", "components/web/management/extra.c"):
            lines = services_image() + [f"0000000000400000 R crux_web_route_probe_echo\t{ROOT}/{source}:1"]
            found = service_errors(lines)
            self.assertTrue(any("crux_web_route_probe_echo: a route that no component route source defines"
                                in error for error in found), found)

    def test_a_route_from_a_generated_source_that_is_not_the_folder_fails(self):
        lines = services_image() + [
            f"0000000000400000 R crux_web_route_crux_web_asset_route_9\t{ROOT}/build/x/other/crux_web_assets.c:5"]
        self.assertTrue(any("crux_web_asset_route_9: a route that no component" in error
                            for error in service_errors(lines)))

    def test_a_missing_route_fails(self):
        lines = [line for line in services_image() if " crux_web_route_mgmt_network_put\t" not in line]
        self.assertTrue(any("crux_web_route_mgmt_network_put: a route of" in error for error in service_errors(lines)))

    def test_a_route_moved_to_another_source_fails(self):
        lines = [line.replace(f"{ROOT}/components/web/management/web_management_routes.c",
                              f"{ROOT}/tests/native/management_services/src/routes.c")
                 if " crux_web_route_mgmt_status\t" in line else line for line in services_image()]
        self.assertTrue(any("crux_web_route_mgmt_status: comes from" in error for error in service_errors(lines)))

    def test_a_missing_asset_folder_fails(self):
        lines = [line for line in services_image() if "crux_web_asset_route_" not in line]
        self.assertIn("no route of the packaged web folder is in the image", service_errors(lines))

    def test_the_route_list_is_the_components_list_and_the_folder(self):
        def entries(path):
            return [line.strip() for line in path.read_text().splitlines()
                    if line.strip() and not line.startswith("#")]
        image = entries(REPOSITORY / "tests/native/management_services/reviewed-routes.txt")
        components = entries(REPOSITORY / "components/web/reviewed-routes.txt")
        self.assertEqual(image, components + ["public GET @assets"])


if __name__ == "__main__":
    unittest.main()
