"""The public headers of Access, the credential store and the security events (ADR-0004, T2).

These checks run on the host. They compile each header alone and read the CMake definitions of the
public targets that the Zephyr module creates. They show that a product reaches the start of the
credential store through `<crux/credential_store.h>` and `crux::credential_store`, that each header
pulls in only its capability's own Crux headers and no frozen or native header, and that the raw
credential and first-account declarations stay in the private adapter headers.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = ROOT / "components"
CC = os.environ.get("CC", "cc")
FLAGS = ["-std=c17", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
FROZEN = ("thread.h", "sync.h", "queue.h", "serial.h")

# header -> (component, the Crux headers that it may pull in, the include roots of those)
HEADERS = {
    "access.h": ("access", {"access.h"}, ["access/include"]),
    "credential_store.h": ("credential", {"credential_store.h", "status.h", "file.h"},
                           ["credential/include", "base/include", "file/include"]),
    "security_event.h": ("security_event", {"security_event.h", "access.h", "status.h"},
                         ["security_event/include", "access/include", "base/include"]),
}

# The credential store's start, which ADR-0004 (section 2, decision C2) publishes as ordinary service
# API. A reference to each function makes the compiler check the declaration.
START_USES = """
crux_credential_result_t (*start)(crux_file_root_t, crux_file_root_t) = crux_credential_start;
int main(void) { return start ? 0 : 1; }
"""

# What the private adapter headers keep, and ADR-0004 section 2 says is not published.
CREDENTIAL_PRIVATE = ("crux_credential_read", "crux_credential_write", "crux_credential_key_ensure",
                      "crux_credential_key_replace", "crux_credential_key_parse",
                      "crux_credential_key_public", "crux_credential_erase",
                      "crux_credential_roots", "crux_credential_state(")
ACCESS_PRIVATE = ("crux_access_provision_first_account",)


def public_path(name):
    component = HEADERS[name][0]
    return COMPONENTS / component / "include" / "crux" / name


def compile_source(source, roots, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "tu.c"
        path.write_text(source)
        command = [CC, *FLAGS, *extra, *[arg for root in roots for arg in ("-I", str(root))],
                   "-fsyntax-only", str(path)]
        return subprocess.run(command, capture_output=True, text=True)


def roots_of(name):
    return [COMPONENTS / relative for relative in HEADERS[name][2]]


class PublicHeaders(unittest.TestCase):
    def test_each_header_compiles_alone_with_only_its_own_roots(self):
        for name in HEADERS:
            with self.subTest(header=name):
                result = compile_source(f"#include <crux/{name}>\nint main(void) {{ return 0; }}\n",
                                        roots_of(name))
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_each_header_pulls_in_only_its_capabilitys_crux_headers(self):
        for name, (_component, allowed, _roots) in HEADERS.items():
            with self.subTest(header=name):
                result = compile_source(f"#include <crux/{name}>\nint main(void) {{ return 0; }}\n",
                                        roots_of(name), ["-H"])
                self.assertEqual(result.returncode, 0, result.stderr)
                included = [line.lstrip(". ").strip() for line in result.stderr.splitlines()
                            if line.startswith(".")]
                crux = {Path(path).name for path in included if "/crux/" in path}
                self.assertEqual(crux, allowed)
                for path in crux:
                    self.assertNotIn(path, FROZEN)
                outside = [path for path in included if not path.startswith(str(ROOT))]
                for path in outside:
                    self.assertNotRegex(path.lower(), r"zephyr|nxp|cmsis|mcux|/boards?/|wolf")

    def test_no_header_names_a_frozen_or_native_header(self):
        for name in HEADERS:
            text = public_path(name).read_text()
            with self.subTest(header=name):
                for frozen in FROZEN:
                    self.assertNotRegex(text, rf"#\s*include\s*[<\"]crux/{re.escape(frozen)}")
                self.assertNotRegex(text, r"#\s*include\s*[<\"](zephyr|wolf|mbedtls|psa)/")

    def test_the_credential_start_is_declared_in_the_public_header(self):
        result = compile_source("#include <crux/credential_store.h>\n" + START_USES,
                                roots_of("credential_store.h"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_private_header_declares_the_start_and_the_result_type_no_second_time(self):
        text = (COMPONENTS / "credential/adapter/crux_credential_adapter.h").read_text()
        self.assertNotRegex(text, r"crux_credential_result_t\s+crux_credential_start\s*\(")
        self.assertNotRegex(text, r"\}\s*crux_credential_result_t\s*;")
        self.assertRegex(text, r"#include\s+<crux/credential_store\.h>")

    def test_the_private_header_still_compiles_with_the_public_one(self):
        source = ('#include "crux_credential_adapter.h"\n'
                  "crux_credential_result_t (*read)(crux_credential_record_t, void *, size_t, size_t *)"
                  " = crux_credential_read;\n"
                  "crux_credential_result_t (*start)(crux_file_root_t, crux_file_root_t)"
                  " = crux_credential_start;\n"
                  "int main(void) { return read && start ? 0 : 1; }\n")
        result = compile_source(source, [COMPONENTS / "credential/adapter", *roots_of("credential_store.h")])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_raw_credential_operations_are_not_published(self):
        text = public_path("credential_store.h").read_text()
        for name in CREDENTIAL_PRIVATE:
            self.assertNotIn(name, text, name)
        self.assertNotIn("crux_credential_record_t", text)
        self.assertNotIn("CRUX_CREDENTIAL_RECORD_", text)

    def test_first_account_provisioning_is_not_published(self):
        for name in HEADERS:
            text = public_path(name).read_text()
            for function in ACCESS_PRIVATE:
                self.assertNotIn(function, text, f"{name}: {function}")

    def test_each_public_path_is_unique_across_components(self):
        for name in HEADERS:
            found = sorted(p.relative_to(ROOT).as_posix() for p in COMPONENTS.rglob(name)
                           if "include/crux" in p.as_posix())
            self.assertEqual(found, [public_path(name).relative_to(ROOT).as_posix()], name)

    def test_each_header_names_its_tier(self):
        # ADR-0001 decision 4: Access has no Zephyr composition outside tests/, so it is Experimental,
        # and so are the two capabilities that Access wires to.
        for name in HEADERS:
            text = public_path(name).read_text()
            self.assertRegex(text.split("*/", 1)[0], r"Status:\s+Experimental", name)

    def test_lines_fit_the_column_limit_of_the_format_check(self):
        for name in HEADERS:
            for number, line in enumerate(public_path(name).read_text().splitlines(), 1):
                self.assertLessEqual(len(line), 100, f"{name}:{number}")

    def test_each_header_is_usable_from_c_plus_plus(self):
        for name in HEADERS:
            text = public_path(name).read_text()
            self.assertIn('extern "C" {', text, name)

    def test_the_start_documents_ownership_context_and_order(self):
        text = " ".join(public_path("credential_store.h").read_text().replace("\n *", " ").split())
        for phrase in ("crux_credential_start", "Thread context", "once", "before crux_access_init",
                       "File root", "CRUX_CREDENTIAL_ERR_UNUSABLE", "CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT"):
            self.assertIn(phrase, text, phrase)


class PublicTargets(unittest.TestCase):
    """The Zephyr module gives each capability a public target that exposes the include root only."""

    CASES = {
        "access": ("crux_access_public_target", "crux::access", "crux_access"),
        "credential": ("crux_credential_public_target", "crux::credential_store", "crux_credential_store"),
        "security_event": ("crux_security_event_public_target", "crux::security_event",
                           "crux_security_event"),
    }

    @staticmethod
    def port_text(component):
        return (COMPONENTS / component / "ports/zephyr/CMakeLists.txt").read_text()

    @classmethod
    def body(cls, component):
        function = cls.CASES[component][0]
        match = re.search(rf"function\({function}\)(.*?)endfunction\(\)", cls.port_text(component), re.S)
        return match.group(1) if match else None

    def test_each_port_defines_its_public_target_as_a_function_and_calls_it_once(self):
        for component, (function, _alias, _name) in self.CASES.items():
            with self.subTest(component=component):
                self.assertIsNotNone(self.body(component))
                calls = re.findall(rf"^{function}\(\)\s*$", self.port_text(component), re.M)
                self.assertEqual(len(calls), 1)

    def test_each_target_is_an_interface_library_with_a_crux_alias(self):
        for component, (_function, alias, name) in self.CASES.items():
            text = self.body(component) or ""
            with self.subTest(component=component):
                self.assertIn(f"add_library({name} INTERFACE)", text)
                self.assertIn(f"add_library({alias} ALIAS {name})", text)

    def test_each_target_exposes_only_the_public_include_root_and_the_portable_contracts(self):
        allowed = {"access": set(),
                   "credential": {"crux::base", "crux::file"},
                   "security_event": {"crux::base", "crux::access"}}
        for component in self.CASES:
            text = self.body(component) or ""
            with self.subTest(component=component):
                self.assertIn("/include\"", text)
                for forbidden in ("/src", "/adapter", "/ports", "zephyr_interface", "PUBLIC",
                                  "target_link_options", "target_compile", "crux_access_zephyr",
                                  "crux_credential_zephyr", "crux_security_event_zephyr"):
                    self.assertNotIn(forbidden, text)
                links = re.findall(r"target_link_libraries\(\w+ INTERFACE ([^)]*)\)", text)
                linked = {word for entry in links for word in entry.split()}
                self.assertEqual(linked, allowed[component])

    def test_the_providers_are_not_linked_by_the_public_targets(self):
        # The libraries are registered with the Zephyr link by zephyr_append_cmake_library.
        for component in self.CASES:
            self.assertIn("zephyr_append_cmake_library", self.port_text(component))
            self.assertNotIn("zephyr_append_cmake_library", self.body(component) or "")

    def test_each_port_checks_its_target_with_the_native_free_consumer_check(self):
        for component, (_function, alias, _name) in self.CASES.items():
            self.assertIn(f"DEFER CALL crux_assert_portable_usage {alias}", self.port_text(component), component)

    def test_the_credential_and_access_adapters_stay_unreachable(self):
        self.assertIn("DEFER CALL crux_assert_credential_adapter_unreachable crux::credential_store",
                      self.port_text("credential"))
        self.assertIn("DEFER CALL crux_assert_access_adapter_unreachable crux::access",
                      self.port_text("access"))
        self.assertIn("DEFER CALL crux_assert_access_adapter_unreachable crux::security_event",
                      self.port_text("security_event"))

    def test_the_host_credential_api_carries_the_file_contract_the_header_includes(self):
        text = (COMPONENTS / "credential/CMakeLists.txt").read_text()
        match = re.search(r"target_link_libraries\(crux_credential_api INTERFACE ([^)]*)\)", text)
        self.assertIsNotNone(match)
        self.assertIn("crux_file_api", match.group(1).split())


if __name__ == "__main__":
    unittest.main()
