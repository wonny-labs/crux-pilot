#!/usr/bin/env python3
"""Check what the target layout of the status and configuration services cannot enforce alone.

- Their public headers include only public Crux headers and ISO C, with no native, TLS or adapter
  types.
- The status service never reads the reset cause, or arms or feeds a watchdog (decision C8).
- The configuration service never loads a parameter, and no fake is reachable from a product build,
  which the consumer project in tests/mgmt_services_consumer checks when it configures.
- The status service links the character rule's library and not the mechanism, because a link that
  takes a whole archive would otherwise need a Configuration provider.
"""
from pathlib import Path
import re
import runpy
import sys

REPO = Path(__file__).resolve().parents[1]
ALLOWED_INCLUDES = {
    REPO / "components/config_service/include/crux/config_service.h": {
        "stdbool.h", "stddef.h", "crux/access.h", "crux/configuration.h"},
    REPO / "components/mgmt_status/include/crux/management_status.h": {
        "stdbool.h", "stddef.h", "stdint.h", "crux/access.h", "crux/status.h", "crux/system.h"},
    REPO / "components/configuration/include/crux/configuration.h": {
        "stdbool.h", "stddef.h", "stdint.h"},
}
PROVIDER_TOKEN = re.compile(
    r"\b(?:wolf\w*|WOLF\w*|mbedtls\w*|MBEDTLS\w*|psa_\w+|PSA_\w+|ssl_\w+|SSL_\w+|WS_\w+|tls_\w+|TLS_\w+)\b")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
FORBIDDEN_CALLS = {
    REPO / "components/mgmt_status": ("crux_system_reset_cause_get", "crux_system_reset_cause_clear",
                                      "crux_system_reset", "crux_watchdog_start",
                                      "crux_watchdog_feed"),
    REPO / "components/config_service": ("crux_config_load", "crux_access_login_password",
                                         "crux_access_session_create"),
}
ADAPTER_HEADER = "crux_access_adapter.h"


def header_errors():
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for path, allowed in ALLOWED_INCLUDES.items():
        relative = path.relative_to(REPO)
        for line, reason in native["violations"](path, public=True, networking=False):
            errors.append(f"{relative}:{line}: {reason}")
        source = path.read_text(encoding="utf-8")
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', source, re.MULTILINE):
            if match.group(1) not in allowed:
                errors.append(f"{relative}: unexpected include {match.group(1)}")
        for match in PROVIDER_TOKEN.finditer(COMMENT.sub("", source)):
            errors.append(f"{relative}: provider token {match.group()}")
    return errors


def call_errors():
    errors = []
    for root, calls in FORBIDDEN_CALLS.items():
        for path in sorted(root.rglob("*.[ch]")):
            source = COMMENT.sub("", path.read_text(encoding="utf-8"))
            relative = path.relative_to(REPO)
            if ADAPTER_HEADER in source:
                errors.append(f"{relative}: includes the Access adapter header")
            for call in calls:
                if re.search(rf"\b{call}\s*\(", source):
                    errors.append(f"{relative}: calls {call}")
    return errors


def link_errors():
    path = REPO / "components/mgmt_status/CMakeLists.txt"
    lines = [line for line in path.read_text(encoding="utf-8").splitlines()
             if not line.lstrip().startswith("#")]
    text = "\n".join(lines)
    errors = []
    if "crux_configuration_text" not in text:
        errors.append(f"{path.relative_to(REPO)}: must link crux_configuration_text")
    if re.search(r"crux::configuration\b|\bcrux_configuration\b", text):
        errors.append(f"{path.relative_to(REPO)}: must not link the Configuration mechanism")
    return errors


def main():
    errors = header_errors() + call_errors() + link_errors()
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print("status and configuration service boundaries hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
