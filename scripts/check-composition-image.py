#!/usr/bin/env python3
"""Check on a built management image that every port has its real provider and no fake is linked.

It reads the symbols of the image with their source files, from `nm --defined-only -l`, so it sees what
the final link took, whatever a build file says. A symbol with no line information cannot prove where it
came from, so it fails. The test in tests/build holds this check to images that link a fake.
"""
import argparse
import re
import subprocess
import sys

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
    "crux_mgmt_status_port_has_security_events": "components/mgmt_status/src/source_security_events.c",
    "crux_mgmt_status_port_security_events": "components/mgmt_status/src/source_security_events.c",
    "crux_mgmt_status_port_has_credential": "components/mgmt_status/src/source_credential.c",
    "crux_mgmt_status_port_credential": "components/mgmt_status/src/source_credential.c",
}
# Anything from a test double. Each name is the prefix of a fake's own functions.
FAKE_SYMBOL = re.compile(
    r"^(?:access_fake_|credential_fake_|security_event_fake_|config_events_|mgmt_status_fake_|"
    r"crux_access_test_|crux_credential_test_|crux_security_event_test_|crux_file_fake_)")
FAKE_SOURCE = re.compile(r"/tests/(?:access|credential|config_service|mgmt_status|security_event|file)/")
PROFILES = {
    "mbed": {"present": ("mbedtls_ssl_setup", "psa_crypto_init"), "absent": ("wolfSSL_new", "wolfSSL_Init")},
    "wolf": {"present": ("wolfSSL_new", "psa_crypto_init"), "absent": ("mbedtls_ssl_setup",)},
}
LINE = re.compile(r"^[0-9a-fA-F]+ (\S) (\S+)(?:\t(\S+?):\d+)?\s*$")


def read_symbols(text):
    symbols = {}
    for line in text.splitlines():
        match = LINE.match(line)
        if match and match.group(1) in "tTwWdDbBrR":
            symbols.setdefault(match.group(2), []).append(match.group(3))
    return symbols


def errors_in(symbols, profile):
    errors = []
    for port, source in PORTS.items():
        found = symbols.get(port)
        if not found:
            errors.append(f"{port}: no provider in the image")
            continue
        if len(found) != 1:
            errors.append(f"{port}: {len(found)} definitions")
        if found[0] is None or not found[0].endswith("/" + source):
            errors.append(f"{port}: comes from {found[0]}, and must come from {source}")
    for name, sources in symbols.items():
        if FAKE_SYMBOL.match(name):
            errors.append(f"{name}: a test double is linked")
        for source in sources:
            if source and FAKE_SOURCE.search(source):
                errors.append(f"{name}: comes from test code, {source}")
    for name in PROFILES[profile]["present"]:
        if name not in symbols:
            errors.append(f"{name}: the {profile} profile's provider is not in the image")
    for name in PROFILES[profile]["absent"]:
        if name in symbols:
            errors.append(f"{name}: the other profile's provider is in the image")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("--profile", required=True, choices=sorted(PROFILES))
    args = parser.parse_args()
    output = subprocess.run(["nm", "--defined-only", "-l", args.image], check=True, capture_output=True,
                            text=True).stdout
    errors = errors_in(read_symbols(output), args.profile)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"{args.image}: {len(PORTS)} ports on their real providers, no test double, {args.profile} profile")
    return 0


if __name__ == "__main__":
    sys.exit(main())
