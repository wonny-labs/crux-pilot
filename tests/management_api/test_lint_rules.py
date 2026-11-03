"""Check that scripts/check-management-api.sh rejects broken copies of the specification.

Run with: python3 -m unittest discover -s tests/management_api -v
It needs the pinned Node.js, like the script. A new rule gets a broken copy here.
"""

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SPEC = (REPO / "web/api/openapi.yaml").read_text()
SCRIPT = REPO / "scripts/check-management-api.sh"


def operation(text, operation_id):
    """Return the start and end of one operation's block."""
    start = text.index(f"      operationId: {operation_id}\n")
    match = re.compile(r"^ {0,4}\S", re.M).search(text, start)
    return start, match.start()


def in_operation(operation_id, old, new):
    def mutate(text):
        start, end = operation(text, operation_id)
        block = text[start:end]
        if old not in block:
            raise AssertionError(f"{old!r} is not in {operation_id}")
        return text[:start] + block.replace(old, new, 1) + text[end:]

    return mutate


def anywhere(old, new):
    def mutate(text):
        if old not in text:
            raise AssertionError(f"{old!r} is not in the specification")
        return text.replace(old, new, 1)

    return mutate


def drop_response(operation_id, code):
    def mutate(text):
        start, end = operation(text, operation_id)
        block = text[start:end]
        pattern = re.compile(rf"^        '{code}':\n( {{10,}}.*\n)+", re.M)
        replaced, count = pattern.subn("", block, count=1)
        if count != 1:
            raise AssertionError(f"{code} is not in {operation_id}")
        return text[:start] + replaced + text[end:]

    return mutate


def add_method(path, method):
    return anywhere(
        f"  {path}:\n",
        f"  {path}:\n    {method}:\n      summary: Extra\n      responses:\n"
        f"        '200':\n          description: Extra.\n",
    )


SESSION = "      security:\n        - sessionCookie: []\n"
SESSION_CSRF = "      security:\n        - sessionCookie: []\n          xsrfHeader: []\n"

# Name, the mutation, and a rule that must report it.
BROKEN_COPIES = [
    ("access class missing", in_operation("getNetwork", "      x-crux-access: network.read\n", ""),
     "crux-access-class-present"),
    ("access class invalid", in_operation("getNetwork", "x-crux-access: network.read", "x-crux-access: Network Read"),
     "crux-access-class"),
    ("public route with security", in_operation("createSession", "      security: []\n", SESSION),
     "crux-public-has-no-security"),
    ("no security on a route", in_operation("getNetwork", SESSION, ""), "crux-security-declared"),
    ("POST reboot with empty security", in_operation("requestReboot", SESSION_CSRF, "      security: []\n"),
     "crux-session-security-not-empty"),
    ("GET network with empty security", in_operation("getNetwork", SESSION, "      security: []\n"),
     "crux-session-security-not-empty"),
    ("GET me with an empty requirement", in_operation("getCurrentUser", SESSION, "      security:\n        - {}\n"),
     "crux-security-needs-session-cookie"),
    ("PUT network with the CSRF header only",
     in_operation("putNetwork", SESSION_CSRF, "      security:\n        - xsrfHeader: []\n"),
     "crux-security-needs-session-cookie"),
    ("PUT network with alternative requirements",
     in_operation("putNetwork", SESSION_CSRF, "      security:\n        - sessionCookie: []\n        - xsrfHeader: []\n"),
     "crux-security-needs-session-cookie"),
    ("state change without CSRF", in_operation("putNetwork", SESSION_CSRF, SESSION), "crux-state-change-needs-csrf"),
    ("GET with CSRF", in_operation("getNetwork", SESSION, SESSION_CSRF), "crux-read-has-no-csrf"),
    ("patch operation", add_method("/api/v1/network", "patch"), "crux-methods"),
    ("head operation", add_method("/api/v1/network", "head"), "crux-methods"),
    ("options operation", add_method("/api/v1/network", "options"), "crux-methods"),
    ("trace operation", add_method("/api/v1/network", "trace"), "crux-methods"),
    ("204 response", in_operation("getRebootState", "      responses:\n",
                                  "      responses:\n        '204':\n          description: None.\n"),
     "crux-no-chunked-forbidden-status"),
    ("session route without 401", drop_response("getNetwork", "401"), "crux-session-routes-have-401"),
    ("fixed permission without 403", drop_response("getNetwork", "403"), "crux-permission-routes-have-403"),
    ("state change without 403", drop_response("deleteSession", "403"), "crux-state-change-has-403"),
    ("missing 409", drop_response("getStatus", "409"), "crux-busy-409"),
    ("409 with a JSON body",
     lambda text: re.sub(r"(    Busy:\n      description: )\|\n(        .*\n)+",
                         r"\1Busy\n      content:\n        application/json:\n          schema:\n"
                         r"            $ref: '#/components/schemas/Error'\n          examples:\n"
                         r"            busy:\n              value:\n                code: busy\n",
                         text, count=1),
     "crux-busy-409-no-body"),
    ("missing 503", drop_response("getStatus", "503"), "crux-unavailable-503"),
    ("503 with no Retry-After",
     lambda text: re.sub(r"(    ServiceUnavailable:\n      description: \|\n(        .*\n)+)      headers:\n"
                         r"        Retry-After:\n          \$ref: .*\n", r"\1", text, count=1),
     "crux-retry-after"),
    ("login route may reveal not_ready",
     in_operation("createSession", "#/components/responses/LoginBusy", "#/components/responses/ServiceUnavailable"),
     "crux-login-503-busy-only"),
    ("poll may reveal not_ready",
     in_operation("getSessionAttempt", "#/components/responses/LoginBusy", "#/components/responses/ServiceUnavailable"),
     "crux-login-503-busy-only"),
    ("invalid example state", in_operation("getSessionAttempt", "state: pending", "state: done"),
     "oas3-valid-media-example"),
    ("wrong code for a status", anywhere("value:\n                code: unauthorized", "value:\n                code: busy"),
     "oas3-valid-media-example"),
    ("refused with no reason", in_operation("getRebootState", "state: refused\n                    reason: Machine cycle running",
                                            "state: refused"), "oas3-valid-media-example"),
    ("fields with no code", in_operation("getSessionAttempt", "                    code: invalid_field\n", ""),
     "oas3-valid-media-example"),
    ("short attempt id", in_operation("createSession", "id: k3Jq9sZp0vXw2LmN7bYt1A", "id: short"),
     "oas3-valid-media-example"),
    ("missing examples", in_operation("requestReboot", "              examples:\n                accepted:\n"
                                      "                  summary: Reboot accepted\n                  value:\n"
                                      "                    state: pending\n", ""), "crux-json-has-named-examples"),
    ("non-JSON body", in_operation("requestReboot", "application/json:", "text/plain:"), "crux-json-only"),
    ("unused component", anywhere("  schemas:\n    ErrorCode:", "  schemas:\n    Unused:\n      type: string\n    ErrorCode:"),
     "oas3-unused-component"),
    ("not valid YAML", anywhere("openapi: 3.0.3", "openapi: [3.0.3"), "parser"),
]


def lint(path):
    return subprocess.run([str(SCRIPT), str(path)], capture_output=True, text=True, env=os.environ, check=False)


class LintRules(unittest.TestCase):
    def test_specification_passes(self):
        result = lint(REPO / "web/api/openapi.yaml")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_broken_copies_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / "openapi.yaml"
            for name, mutate, rule in BROKEN_COPIES:
                with self.subTest(name):
                    mutated = mutate(SPEC)
                    self.assertNotEqual(mutated, SPEC)
                    copy.write_text(mutated)
                    result = lint(copy)
                    output = result.stdout + result.stderr
                    self.assertEqual(result.returncode, 1, output)
                    self.assertRegex(output, rf"\s{re.escape(rule)}\s", output)
                    print(f"rejected: {name}: {rule}")


if __name__ == "__main__":
    unittest.main()
