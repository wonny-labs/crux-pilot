#!/usr/bin/env python3
"""Check on a built management image that each listed port has its real provider and no fake is linked.

It reads the symbols of the image with their source files, from `nm --defined-only -l`, so it sees what
the final link took, whatever a build file says. A source must be exactly the listed file under the
repository, and a symbol with no line information cannot prove where it came from, so it fails. PSA and Time's
clock must come from their providers too. The test in tests/build holds this check to images that link a fake.
It reads symbol names and sources, and it shows nothing about what they do.

--services checks the image of tests/native/management_services (Issue #242) as well: each further service and
port from its real source, the random path through the entropy driver, a route table of exactly the routes that
the components' route sources and the packaged folder define, and, from `objdump -t`, one HTTP service whose every
resource the route table generated, so that no service is missing and no served path is added outside the table.
"""
import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
# Each port, and the source that must provide it, relative to the repository.
PORTS = {
    "crux_access_port_derive": "components/access/ports/psa/access_psa.c",
    "crux_access_port_random": "components/access/ports/psa/access_psa.c",
    "crux_access_port_hmac": "components/access/ports/psa/access_psa.c",
    "crux_access_port_now_ms": "components/access/src/access_clock_time.c",
    "crux_access_port_store_load": "components/credential/access_store/credential_access_store.c",
    "crux_access_port_store_save": "components/credential/access_store/credential_access_store.c",
    "crux_access_port_event": "components/security_event/src/security_event_access_port.c",
    "crux_credential_port_declaration": "components/credential/ports/psa/credential_psa.c",
    "crux_credential_port_seal": "components/credential/ports/psa/credential_psa.c",
    "crux_credential_port_open": "components/credential/ports/psa/credential_psa.c",
    "crux_credential_port_generate_key": "components/credential/ports/psa/credential_psa.c",
    "crux_credential_port_random": "components/credential/ports/psa/credential_psa.c",
    "crux_credential_port_event": "components/credential/src/credential_security_events.c",
    "crux_credential_psa_device_id": "components/credential/ports/zephyr/credential_device_id.c",
    "crux_config_service_port_event": "components/config_service/src/config_service_events.c",
    "crux_mgmt_status_port_uptime_ms": "components/mgmt_status/src/source_time.c",
    "crux_mgmt_status_port_clock": "components/mgmt_status/src/source_time.c",
    "crux_mgmt_status_port_system": "components/mgmt_status/src/source_system.c",
    "crux_mgmt_status_port_net": "components/mgmt_status/src/source_netif.c",
    "crux_config_record_write": "components/configuration/src/configuration_no_provider.c",
    "crux_monotonic_now": "components/time/src/time_clock.c",
    "crux_mgmt_status_port_has_security_events": "components/mgmt_status/src/source_security_events.c",
    "crux_mgmt_status_port_security_events": "components/mgmt_status/src/source_security_events.c",
    "crux_mgmt_status_port_has_credential": "components/mgmt_status/src/source_credential.c",
    "crux_mgmt_status_port_credential": "components/mgmt_status/src/source_credential.c",
}
# The services image adds these ports, each from its provider (Issue #242).
SERVICE_PORTS = {
    "crux_web_session_port_random": "components/web/ports/psa/web_session_psa.c",
    "crux_web_session_port_hash": "components/web/ports/psa/web_session_psa.c",
    "crux_tls_certificate_port_sha256": "components/tls_certificate/ports/psa/tls_certificate_psa.c",
    "crux_tls_certificate_port_sign": "components/tls_certificate/ports/psa/tls_certificate_psa.c",
    "crux_tls_certificate_port_random": "components/tls_certificate/ports/psa/tls_certificate_psa.c",
    "crux_tls_certificate_port_event": "components/tls_certificate/src/tls_certificate_events.c",
    "crux_network_settings_port_apply": "components/network_settings/src/network_settings_no_apply.c",
    "crux_network_settings_port_running": "components/network_settings/src/network_settings_no_apply.c",
    "crux_network_settings_port_event": "components/network_settings/src/network_settings_events.c",
    "crux_cli_port_event": "components/cli/src/cli_security_events.c",
    "crux_file_service_port_event": "components/file_service/src/file_service_security_events.c",
}
# One entry point of each service, from its own source: a service left out of the image fails here.
SERVICES = {
    "crux_web_start": "components/web/ports/zephyr/web_http_server.c",
    "crux_web_session_init": "components/web/session/web_session.c",
    "crux_web_management_init": "components/web/management/web_management_save.c",
    "crux_web_assets_serve": "components/web/assets/web_assets.c",
    "crux_network_settings_start": "components/network_settings/src/network_settings.c",
    "crux_network_settings_cli_commands": "components/network_settings/src/network_settings_cli.c",
    "crux_tls_certificate_start": "components/tls_certificate/src/tls_certificate.c",
    "crux_cli_init": "components/cli/src/cli_registry.c",
    "crux_file_service_start": "components/file_service/src/file_service.c",
    "crux_config_service_start": "components/config_service/src/config_service.c",
    "crux_mgmt_status_start": "components/mgmt_status/src/mgmt_status.c",
    "crux_security_event_init": "components/security_event/src/security_event.c",
    "crux_access_init": "components/access/src/access.c",
    "crux_credential_start": "components/credential/src/credential_store.c",
}
# The only route sources the services image may link. Their routes are what the precedents define.
ROUTE_SOURCES = (
    "components/web/session/web_session_routes.c",
    "components/web/management/web_management_routes.c",
    "components/web/management/web_management_network.c",
)
ROUTE_SYMBOL = "crux_web_route_"
ROUTE_TABLE_SYMBOLS = {"crux_web_route_table", "crux_web_route_expected", "crux_web_route_count"}
# A packaged folder's routes are generated, and web_routes.py holds each to the asset handler. The source must be
# exactly what crux_web_assets() writes for `app` in this build.
BUNDLE_SOURCE = "crux_web_assets/app/crux_web_assets.c"
# The HTTP server iterates these output sections, so a resource or a service defined outside the route table, with
# HTTP_RESOURCE_DEFINE or a second HTTP_SERVICE_DEFINE, is served even though no crux route names it.
RESOURCE_SECTION = "http_resource_desc_crux_web_service_area"
SERVICE_SECTION = "http_service_desc_area"
RESOURCE_PREFIX = "http_resource_desc_"
RESOURCE_NAME = re.compile(r"^crux_web_resource_\d{4}$")
RESOURCE_SOURCE = "crux_web_generated/app/crux_web_resources.c"
SERVICE = ("crux_web_service", "components/web/ports/zephyr/web_http_server.c")
# The random path: Zephyr's random API over the entropy driver, which is the declared test-only one.
RANDOM_SOURCES = {"subsys/random/random_entropy_device.c"}
ENTROPY_DRIVER = ("entropy_native_sim_get_entropy", "drivers/entropy/fake_entropy_native_sim.c")
PROFILE_RANDOM = {"mbed": ("mbedtls_psa_external_get_random", "modules/mbedtls/zephyr_entropy.c"),
                  # wolfCrypt's Zephyr seed reads the chosen entropy driver, so one Hash-DRBG under wolfPSA and the
                  # TLS backend is seeded from it. A custom seed would replace this function.
                  "wolf": ("wc_GenerateSeed", "wolfcrypt/src/random.c")}
# Zephyr's random entry points: each that the image holds must come from the entropy device source, with its line.
RANDOM_ENTRY_POINTS = ("z_impl_sys_rand_get", "z_impl_sys_csrand_get", "z_impl_sys_rand32_get")
# Anything from a test double. Each name is the prefix of a fake's own functions.
FAKE_SYMBOL = re.compile(
    r"^(?:access_fake_|credential_fake_|security_event_fake_|config_events_|mgmt_status_fake_|"
    r"crux_access_test_|crux_credential_test_|crux_security_event_test_|crux_file_fake_)")
# Any test code but the composition itself and its declared certificate, wherever the tree is.
TEST_SOURCE = re.compile(r"/tests/(?!native/management_composition/|native/tls_offload_probe/credentials/)")
# The services image's own source is test code too, and nothing else is allowed.
SERVICES_TEST_SOURCE = re.compile(
    r"/tests/(?!native/management_composition/|native/tls_offload_probe/credentials/|native/management_services/)")
# What must be in the image, and part of the path of the module it must come from.
PROFILES = {
    "mbed": {"present": {"mbedtls_ssl_setup": "/mbedtls/", "psa_crypto_init": "/tf-psa-crypto/"},
             "absent": ("wolfSSL_new", "wolfSSL_Init")},
    "wolf": {"present": {"wolfSSL_new": "/wolfssl/", "psa_crypto_init": "/wolfpsa/"},
             "absent": ("mbedtls_ssl_setup",)},
}
LINE = re.compile(r"^[0-9a-fA-F]+ (\S) (\S+)(?:\t(\S+?):\d+)?\s*$")


class Symbols(dict):
    """Each defined name and its sources, and the names of data objects apart, for the route table."""

    def __init__(self):
        super().__init__()
        self.data = set()


def read_symbols(text):
    symbols = Symbols()
    for line in text.splitlines():
        match = LINE.match(line)
        if match and match.group(1) in "tTwWdDbBrR":
            symbols.setdefault(match.group(2), []).append(match.group(3))
            if match.group(1) in "dDbBrR":
                symbols.data.add(match.group(2))
    return symbols


def load_web_routes():
    spec = importlib.util.spec_from_file_location("web_routes", REPOSITORY / "components/web/tools/web_routes.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expected_routes(root=REPOSITORY):
    """Each route symbol that the components' route sources define, with its source."""
    web_routes = load_web_routes()
    expected = {}
    for source in ROUTE_SOURCES:
        path = REPOSITORY / source
        for route in web_routes.parse_source(path.read_text(encoding="utf-8"), str(path)):
            expected[ROUTE_SYMBOL + route.name] = f"{root}/{source}"
    return expected


def route_errors(symbols, root=REPOSITORY, build=None, expected=None):
    expected = expected_routes(root) if expected is None else expected
    errors = []
    # Routes are data objects. The core's own functions share the prefix.
    data = getattr(symbols, "data", set(symbols))
    linked = {name: sources for name, sources in symbols.items()
              if name.startswith(ROUTE_SYMBOL) and name in data and name not in ROUTE_TABLE_SYMBOLS}
    for name, source in sorted(expected.items()):
        if name not in linked:
            errors.append(f"{name}: a route of {source} is not in the image")
        elif linked[name] != [source]:
            errors.append(f"{name}: comes from {linked[name]}, and must come from {source}")
    bundle = 0
    for name, sources in sorted(linked.items()):
        if name in expected:
            continue
        if build and all(source == f"{build}/{BUNDLE_SOURCE}" for source in sources):
            bundle += 1
            continue
        errors.append(f"{name}: a route that no component route source defines, from {sources}")
    if bundle == 0:
        errors.append("no route of the packaged web folder is in the image")
    for name in ROUTE_TABLE_SYMBOLS:
        if name not in symbols:
            errors.append(f"{name}: the generated route table is not in the image")
    return errors


OBJDUMP_LINE = re.compile(r"^[0-9a-fA-F]+ (.{7}) (\S+)\t[0-9a-fA-F]+\s+(?:\.hidden\s+)?(\S+)\s*$")


def read_sections(text):
    """Each data object's output section, from objdump -t. Section bounds are not objects, so they are left out."""
    sections = {}
    for line in text.splitlines():
        match = OBJDUMP_LINE.match(line)
        if match and "O" in match.group(1):
            sections.setdefault(match.group(3), []).append(match.group(2))
    return sections


def http_errors(symbols, sections, root=REPOSITORY, build=None):
    errors = []
    services = [name for name, found in sections.items() if SERVICE_SECTION in found]
    if not services:
        errors.append("no HTTP service in the image")
    for name in services:
        if name != SERVICE[0] or symbols.get(name) != [f"{root}/{SERVICE[1]}"]:
            errors.append(f"{name}: an HTTP service other than {SERVICE[0]} from {SERVICE[1]}, from {symbols.get(name)}")
    for name, found in sections.items():
        for section in found:
            if not section.startswith(RESOURCE_PREFIX):
                continue
            if section != RESOURCE_SECTION:
                errors.append(f"{name}: a resource of another HTTP service, in {section}")
            elif not RESOURCE_NAME.match(name) or not build or \
                    symbols.get(name) != [f"{build}/{RESOURCE_SOURCE}"]:
                errors.append(f"{name}: an HTTP resource that the route table did not generate, from {symbols.get(name)}")
    return errors


def service_errors(symbols, profile, root=REPOSITORY, sections=None, build=None):
    errors = []
    for table in (SERVICE_PORTS, SERVICES):
        for name, source in table.items():
            found = symbols.get(name)
            if not found:
                errors.append(f"{name}: not in the image, so its service or provider is missing")
            elif found != [f"{root}/{source}"]:
                errors.append(f"{name}: comes from {found}, and must come from {root}/{source}")
    for name, sources in symbols.items():
        for source in sources:
            if source and "/subsys/random/" in source and \
                    not any(source.endswith("/" + allowed) for allowed in RANDOM_SOURCES):
                errors.append(f"{name}: a random source other than the entropy driver, {source}")
    for name in RANDOM_ENTRY_POINTS:
        for source in symbols.get(name, []):
            if not source or not any(source.endswith("/" + allowed) for allowed in RANDOM_SOURCES):
                errors.append(f"{name}: a random entry point from {source}, and only the entropy device source may "
                              "provide it")
    checks = [ENTROPY_DRIVER] + ([PROFILE_RANDOM[profile]] if profile in PROFILE_RANDOM else [])
    for name, suffix in checks:
        found = symbols.get(name)
        if not found or not all(source and source.endswith("/" + suffix) for source in found):
            errors.append(f"{name}: must come from {suffix}, and comes from {found}")
    return errors + route_errors(symbols, root, build) + http_errors(symbols, sections or {}, root, build)


def errors_in(symbols, profile, root=REPOSITORY, services=False, sections=None, build=None):
    errors = []
    test_source = SERVICES_TEST_SOURCE if services else TEST_SOURCE
    for port, source in PORTS.items():
        found = symbols.get(port)
        if not found:
            errors.append(f"{port}: no provider in the image")
            continue
        if len(found) != 1:
            errors.append(f"{port}: {len(found)} definitions")
        if found[0] != f"{root}/{source}":
            errors.append(f"{port}: comes from {found[0]}, and must come from {root}/{source}")
    for name, sources in symbols.items():
        if FAKE_SYMBOL.match(name):
            errors.append(f"{name}: a test double is linked")
        for source in sources:
            if source and test_source.search(source):
                errors.append(f"{name}: comes from test code, {source}")
    for name, module in PROFILES[profile]["present"].items():
        sources = symbols.get(name)
        if not sources:
            errors.append(f"{name}: the {profile} profile's provider is not in the image")
        elif not all(source and module in source for source in sources):
            errors.append(f"{name}: comes from {sources}, and must come from a path with {module}")
    for name in PROFILES[profile]["absent"]:
        if name in symbols:
            errors.append(f"{name}: the other profile's provider is in the image")
    if services:
        errors += service_errors(symbols, profile, root, sections, build)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("--profile", required=True, choices=sorted(PROFILES))
    parser.add_argument("--services", action="store_true",
                        help="the services image: every service, its ports, its random path and its routes")
    args = parser.parse_args()
    output = subprocess.run(["nm", "--defined-only", "-l", args.image], check=True, capture_output=True,
                            text=True).stdout
    sections, build = None, None
    if args.services:
        sections = read_sections(subprocess.run(["objdump", "-t", args.image], check=True, capture_output=True,
                                                text=True).stdout)
        # The image is <build>/zephyr/zephyr.exe, and the generated sources sit under <build>.
        build = str(Path(args.image).resolve().parents[1])
    errors = errors_in(read_symbols(output), args.profile, services=args.services, sections=sections, build=build)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    listed = len(PORTS) + (len(SERVICE_PORTS) + len(SERVICES) if args.services else 0)
    extra = (f", and {len(expected_routes())} component routes and no other, and one HTTP service whose resources the "
             "route table generated") if args.services else ""
    print(f"{args.image}: {listed} listed symbols from their real sources, no test double, {args.profile} profile"
          f"{extra}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
