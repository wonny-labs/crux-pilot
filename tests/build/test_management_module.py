"""Check how the Zephyr module integrates the management service cores (ADR-0004, phase 0).

These checks read the Kconfig and CMake text, so the Host job can run them without a Zephyr
workspace. They hold what a configure-and-build run cannot easily show: the symbols are off by
default, they are marked Experimental, they select Thread and Sync themselves (C6), nothing is
built unless a symbol asks for it, and the module compiles the same portable sources as the Host
build. The build itself is run by tests/native/management_module and scripts/test-management-module.sh.
"""
from pathlib import Path
import re
import unittest

REPO = Path(__file__).resolve().parents[2]
KCONFIG = REPO / "zephyr/Kconfig"
MODULE_CMAKE = REPO / "zephyr/CMakeLists.txt"

# One visible symbol, and the two that it selects because the three are wired to each other: Access
# calls the store and the event provider, the store calls the event provider, and the events call
# Access.
VISIBLE = "CRUX_ACCESS"
HIDDEN = ("CRUX_CREDENTIAL", "CRUX_SECURITY_EVENT")
NEW_SYMBOLS = (VISIBLE, *HIDDEN)

# component -> Host source list variable, the Zephyr port directory, and the extra files that the
# Host build links as providers of another component's ports.
COMPONENTS = {
    "access": ("CRUX_ACCESS_SOURCES", ["src/access_clock_time.c", "ports/psa/access_psa.c"]),
    "credential": ("CRUX_CREDENTIAL_SOURCES", [
        "access_store/credential_access_store.c",
        "ports/psa/credential_psa.c",
        "ports/zephyr/credential_device_id.c",
        "src/credential_security_events.c",
    ]),
    "security_event": ("CRUX_SECURITY_EVENT_SOURCES", ["src/security_event_access_port.c"]),
}


def kconfig_blocks():
    """Return {symbol: text} for each top-level config or menuconfig entry of zephyr/Kconfig."""
    blocks = {}
    name = None
    for line in KCONFIG.read_text().splitlines():
        match = re.match(r"(?:menu)?config (\w+)\s*$", line)
        if match:
            name = match.group(1)
            blocks[name] = []
        elif name is not None and (line.startswith(("\t", "    ")) or not line.strip()):
            blocks[name].append(line)
        elif line.strip():
            name = None
    return {key: "\n".join(value) for key, value in blocks.items()}


def host_sources(component, variable):
    text = (REPO / "components" / component / "CMakeLists.txt").read_text()
    body = re.search(rf"set\({variable}\s+(.*?)\n\)", text, re.DOTALL).group(1)
    return body.split()


def port_sources(component):
    """Return the source paths of the module's port directory, relative to the component."""
    text = (REPO / "components" / component / "ports/zephyr/CMakeLists.txt").read_text()
    return [str((REPO / "components" / component / "ports/zephyr" / path).resolve()
                .relative_to(REPO / "components" / component))
            for path in re.findall(r"^\s*([\w./-]+\.c)\s*\)?\s*$", text, re.MULTILINE)]


# Phase 1a (ADR-0004; the owner approved the target names on 2026-11-15): the one alias that each of these port directories
# defines for its portable target. Every other alias is still refused. The definitions are held to the
# real guards by tests/build/test_service_usage_targets.py.
PHASE_1A_ALIASES = {
    "configuration": "add_library(crux::configuration ALIAS crux_configuration)",
    "config_service": "add_library(crux::config_service ALIAS crux_config_service)",
    "network_settings": "add_library(crux::network_settings ALIAS crux_network_settings)",
    "mgmt_status": "add_library(crux::mgmt_status ALIAS crux_mgmt_status)",
    "file_service": "add_library(crux::file_service ALIAS crux_file_service)",
    "reboot": "add_library(crux::reboot ALIAS crux_reboot)",
}


def without_phase_1a_aliases(text, component):
    """Remove the one alias that this component's port directory may define."""
    return text.replace(PHASE_1A_ALIASES.get(component, "\0"), "")


class KconfigTest(unittest.TestCase):
    def test_every_new_symbol_exists(self):
        blocks = kconfig_blocks()
        for symbol in NEW_SYMBOLS:
            self.assertTrue(symbol in blocks, symbol)

    def test_every_new_symbol_is_off_unless_selected_or_set(self):
        for symbol, text in kconfig_blocks().items():
            if symbol in NEW_SYMBOLS:
                self.assertIsNone(re.search(r"^\s+default\s+y\b", text, re.MULTILINE), symbol)

    def test_every_new_symbol_says_experimental(self):
        blocks = kconfig_blocks()
        for symbol in NEW_SYMBOLS:
            self.assertRegex(blocks[symbol], r"Experimental", symbol)

    def test_the_visible_symbol_has_a_prompt_and_the_others_have_none(self):
        blocks = kconfig_blocks()
        self.assertRegex(blocks[VISIBLE], r"(?m)^\s+bool\s+\"", VISIBLE)
        for symbol in HIDDEN:
            self.assertRegex(blocks[symbol], r"(?m)^\s+bool\s*$", symbol)
            self.assertNotRegex(blocks[symbol], r"(?m)^\s+bool\s+\"", symbol)

    def test_access_selects_what_it_needs_so_a_product_names_one_symbol(self):
        block = kconfig_blocks()[VISIBLE]
        for needed in (*HIDDEN, "CRUX_THREAD", "CRUX_SYNC", "CRUX_TIME"):
            self.assertRegex(block, rf"(?m)^\s+select\s+{needed}\b", needed)

    def test_the_service_symbols_depend_on_the_module(self):
        blocks = kconfig_blocks()
        for symbol in NEW_SYMBOLS:
            self.assertRegex(blocks[symbol], r"depends on [^\n]*CRUX_MODULE", symbol)

    def test_no_wolf_symbol_is_named(self):
        blocks = kconfig_blocks()
        for symbol in NEW_SYMBOLS:
            self.assertNotRegex(blocks[symbol].upper(), r"WOLF", symbol)


class FixtureTest(unittest.TestCase):
    """C6: the fixture's prj.conf names the service symbol, and no Thread, Sync or Time symbol."""

    def test_the_fixture_names_no_thread_sync_or_time_symbol(self):
        text = (REPO / "tests/native/management_module/prj.conf").read_text()
        self.assertIsNone(re.search(r"(?m)^\s*CONFIG_CRUX_(THREAD|SYNC|TIME)\w*\s*=", text))

    def test_the_fixture_names_the_service_symbol(self):
        text = (REPO / "tests/native/management_module/prj.conf").read_text()
        self.assertRegex(text, r"(?m)^CONFIG_CRUX_ACCESS=y$")


class ModuleCMakeTest(unittest.TestCase):
    def test_each_port_directory_is_added_only_under_the_service_symbol(self):
        text = MODULE_CMAKE.read_text()
        guarded = re.search(r"if\(CONFIG_CRUX_ACCESS\)(.*?)\nendif\(\)", text, re.DOTALL)
        self.assertIsNotNone(guarded, "no if(CONFIG_CRUX_ACCESS) block")
        for component in COMPONENTS:
            path = f"components/{component}/ports/zephyr"
            self.assertIn(path, guarded.group(1))
            self.assertEqual(text.count(f'/../{path}"'), 1, f"{path} is added outside the block")

    def test_the_module_names_no_wolf_target_for_the_new_directories(self):
        for component in COMPONENTS:
            text = (REPO / "components" / component / "ports/zephyr/CMakeLists.txt").read_text()
            self.assertNotRegex(text.upper(), r"WOLF", component)

    def test_each_port_directory_exists_and_lists_real_files(self):
        for component in COMPONENTS:
            listed = port_sources(component)
            self.assertTrue(listed, component)
            for relative in listed:
                self.assertTrue((REPO / "components" / component / relative).is_file(),
                                f"{component}: {relative}")


class SourceParityTest(unittest.TestCase):
    """The module builds what the Host build builds, plus the providers that Host links by name."""

    def test_the_module_compiles_every_portable_core_source_of_the_host_build(self):
        for component, (variable, _extra) in COMPONENTS.items():
            listed = set(port_sources(component))
            for relative in host_sources(component, variable):
                self.assertIn(relative.replace("${CMAKE_CURRENT_LIST_DIR}/", ""), listed,
                              f"{component}: {relative} is in the Host build and not in the module")

    def test_the_module_compiles_the_providers_the_host_composition_links(self):
        for component, (_variable, extra) in COMPONENTS.items():
            listed = set(port_sources(component))
            for relative in extra:
                self.assertIn(relative, listed, f"{component}: {relative}")

    def test_the_module_compiles_nothing_else(self):
        for component, (variable, extra) in COMPONENTS.items():
            allowed = set(host_sources(component, variable)) | set(extra)
            self.assertLessEqual(set(port_sources(component)), allowed, component)


SCRIPT = REPO / "scripts/test-management-module.sh"


class ScriptTest(unittest.TestCase):
    """What scripts/test-management-module.sh must do, so that a defect in it fails here first."""

    def setUp(self):
        self.text = SCRIPT.read_text()

    def test_the_image_run_has_a_time_limit(self):
        self.assertRegex(
            self.text, r"(?m)^\s*timeout\s+(-k\s+\d+\s+)?\S+\s+\"\$build/zephyr/zephyr\.exe\"")

    def test_no_nm_output_is_piped_into_grep_quiet(self):
        # grep -q exits at the first match, nm then gets SIGPIPE, and pipefail turns a match into a failure
        # that an `if` reads as "no match". A pipe from nm, or from the script's own wrapper of it, into any
        # grep spelling of quiet is refused.
        self.assertIsNone(re.search(r"(nm|defined_symbols)\b[^\n]*\|\s*grep\b[^\n]*(\s-\w*q|--quiet)",
                                    self.text))

    def default_off_section(self):
        return self.text.split("# Default off")[1]

    def test_the_default_off_check_lists_every_service_symbol_and_entry_point(self):
        section = self.default_off_section()
        symbols = re.search(r"for symbol in ([^;]*?);\s*do", section, re.DOTALL).group(1).replace("\\\n", " ")
        entries = re.search(r"for entry in ([^;]*?);\s*do", section, re.DOTALL).group(1).replace("\\\n", " ")
        for symbol in ("CRUX_ACCESS", "CRUX_CREDENTIAL", "CRUX_SECURITY_EVENT", "CRUX_CONFIG_SERVICE",
                       "CRUX_MGMT_STATUS", "CRUX_CONFIGURATION_CORE"):
            self.assertIn(symbol, symbols.split(), symbol)
        for function in ("crux_access_init", "crux_credential_start", "crux_security_event_init",
                         "crux_config_service_start", "crux_mgmt_status_start"):
            self.assertIn(function, entries.split(), function)

    def test_the_default_off_check_fails_on_a_match_and_reads_the_elf_without_a_pipe(self):
        section = self.default_off_section()
        self.assertRegex(section, r"off_symbols=\"\$\(defined_symbols \"\$off/zephyr/zephyr\.elf\"\)\"")
        self.assertRegex(section, r"! grep -qE[^\n]*<<< \"\$off_symbols\" \|\| fail")
        self.assertRegex(section, r"! grep -q \"\^CONFIG_\$\{symbol\}=y\" \"\$off/zephyr/\.config\" \|\| fail")

    def test_the_header_claims_only_what_the_script_checks(self):
        header = self.text.split("set -euo pipefail")[0]
        self.assertNotIn("libraries are in the link", header)
        self.assertIn("ELF defines the entry point", header)
        # The claim is backed by code: the fixture's ELF is read and each entry point is searched in it.
        self.assertRegex(self.text, r"fixture_symbols=\"\$\(defined_symbols \"\$build/zephyr/zephyr\.elf\"\)\"")
        self.assertRegex(self.text, r"grep -qE[^\n]*<<< \"\$fixture_symbols\" \|\| fail")


# Slice 2: the Configuration service and the status service. Two visible symbols that need Access, and
# one hidden symbol for the Configuration core that both read: the character rule and, until a provider
# exists for a target (#16), the no-provider port.
SLICE2_VISIBLE = {
    "CRUX_CONFIG_SERVICE": {"CRUX_CONFIGURATION_CORE", "CRUX_SYNC"},
    "CRUX_MGMT_STATUS": {"CRUX_CONFIGURATION_CORE", "CRUX_SYNC", "CRUX_TIME"},
}
SLICE2_HIDDEN = ("CRUX_CONFIGURATION_CORE",)
SLICE2_SYMBOLS = (*SLICE2_VISIBLE, *SLICE2_HIDDEN)
# symbol, component, the CMake directory under it relative to the component, and the Host sources that
# the module deliberately leaves out: the variants that omit what the module links.
SLICE2_DIRECTORIES = {
    "CRUX_CONFIG_SERVICE": ("config_service", "ports/zephyr", set()),
    "CRUX_MGMT_STATUS": ("mgmt_status", "ports/zephyr",
                         {"src/source_no_credential.c", "src/source_no_security_events.c"}),
    "CRUX_CONFIGURATION_CORE": ("configuration", "ports/zephyr/core", set()),
}


def listed_sources(component, directory):
    """Return the .c paths of a port directory, relative to the component."""
    base = REPO / "components" / component / directory
    text = (base / "CMakeLists.txt").read_text()
    return {str((base / path).resolve().relative_to(REPO / "components" / component))
            for path in re.findall(r"^\s*([\w./-]+\.c)\s*\)?\s*$", text, re.MULTILINE)}


def host_c_files(component):
    """Return every portable source that the Host CMake of a component names."""
    text = (REPO / "components" / component / "CMakeLists.txt").read_text()
    # A source may be named through the component's own directory, as the Zephyr-direct reboot service does.
    return set(re.findall(
        r"(?<![\w/.-])(?:\$\{CMAKE_CURRENT_LIST_DIR\}/)?((?:src|access_store|ports/\w+)/[\w./-]+\.c)", text))


class Slice2KconfigTest(unittest.TestCase):
    def test_the_symbols_exist_and_are_off_by_default(self):
        blocks = kconfig_blocks()
        for symbol in SLICE2_SYMBOLS:
            self.assertTrue(symbol in blocks, symbol)
            self.assertIsNone(re.search(r"(?m)^\s+default\s+y\b", blocks[symbol]), symbol)

    def test_every_symbol_says_experimental(self):
        blocks = kconfig_blocks()
        for symbol in SLICE2_SYMBOLS:
            self.assertRegex(blocks[symbol], r"Experimental", symbol)

    def test_the_services_have_a_prompt_and_the_core_has_none(self):
        blocks = kconfig_blocks()
        for symbol in SLICE2_VISIBLE:
            self.assertRegex(blocks[symbol], r"(?m)^\s+bool\s+\"", symbol)
        for symbol in SLICE2_HIDDEN:
            self.assertRegex(blocks[symbol], r"(?m)^\s+bool\s*$", symbol)

    def test_each_service_needs_access_and_selects_what_it_uses(self):
        blocks = kconfig_blocks()
        for symbol, selects in SLICE2_VISIBLE.items():
            self.assertRegex(blocks[symbol], r"depends on [^\n]*\bCRUX_ACCESS\b", symbol)
            for needed in selects:
                self.assertRegex(blocks[symbol], rf"(?m)^\s+select\s+{needed}\b", f"{symbol} {needed}")

    def test_no_wolf_symbol_is_named(self):
        blocks = kconfig_blocks()
        for symbol in SLICE2_SYMBOLS:
            self.assertNotRegex(blocks[symbol].upper(), r"WOLF", symbol)


class Slice2CMakeTest(unittest.TestCase):
    def test_each_directory_is_added_once_and_only_under_its_symbol(self):
        text = MODULE_CMAKE.read_text()
        for symbol, (component, directory, _skipped) in SLICE2_DIRECTORIES.items():
            guarded = re.search(rf"if\(CONFIG_{symbol}\)(.*?)\nendif\(\)", text, re.DOTALL)
            self.assertIsNotNone(guarded, f"no if(CONFIG_{symbol}) block")
            path = f"components/{component}/{directory}"
            self.assertEqual(text.count(f'/../{path}"'), 1, path)
            self.assertIn(f'/../{path}"', guarded.group(1), path)

    def test_the_module_compiles_the_host_sources_of_each_component(self):
        for symbol, (component, directory, skipped) in SLICE2_DIRECTORIES.items():
            if symbol == "CRUX_CONFIGURATION_CORE":
                expected = host_c_files(component)
            else:
                expected = host_c_files(component) - skipped
            self.assertEqual(listed_sources(component, directory), expected, symbol)

    def test_no_new_directory_names_a_wolf_target_or_a_crux_alias(self):
        for symbol, (component, directory, _skipped) in SLICE2_DIRECTORIES.items():
            text = (REPO / "components" / component / directory / "CMakeLists.txt").read_text()
            self.assertNotRegex(text.upper(), r"WOLF", symbol)
            self.assertNotRegex(without_phase_1a_aliases(text, component), r"add_library\(crux::", symbol)

    def test_the_configuration_core_is_left_to_the_settings_file_selection_when_it_is_on(self):
        text = (REPO / "components/configuration/ports/zephyr/core/CMakeLists.txt").read_text()
        self.assertIn("CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE", text)


# Slice 3: network settings, the TLS certificate module, the file service and reboot. Each is a visible
# Experimental symbol that needs Access. The network settings service also needs the Configuration
# service, and its apply port is the application's (ADR-0004 C3) unless the no-op port is asked for.
SLICE3_VISIBLE = {
    "CRUX_NETWORK_SETTINGS": ("CRUX_CONFIG_SERVICE", {"CRUX_THREAD", "CRUX_SYNC"}),
    "CRUX_NETWORK_SETTINGS_NO_APPLY": ("CRUX_NETWORK_SETTINGS", set()),
    "CRUX_TLS_CERTIFICATE": ("CRUX_ACCESS", {"CRUX_SYNC"}),
    "CRUX_FILE_SERVICE": ("CRUX_ACCESS", {"CRUX_SYNC"}),
    "CRUX_REBOOT": ("CRUX_ACCESS", {"CRUX_CONFIGURATION_CORE", "CRUX_THREAD", "CRUX_SYNC"}),
}
# symbol -> component, the Host sources the module leaves out, and the sources it adds from a port directory
SLICE3_DIRECTORIES = {
    "CRUX_NETWORK_SETTINGS": ("network_settings", {"src/network_settings_cli.c"}, set()),
    "CRUX_TLS_CERTIFICATE": ("tls_certificate", set(), {"ports/psa/tls_certificate_psa.c"}),
    "CRUX_FILE_SERVICE": ("file_service", set(), set()),
    "CRUX_REBOOT": ("reboot", set(), set()),
}


class Slice3KconfigTest(unittest.TestCase):
    def test_the_symbols_exist_are_off_by_default_and_say_experimental(self):
        blocks = kconfig_blocks()
        for symbol in SLICE3_VISIBLE:
            self.assertTrue(symbol in blocks, symbol)
            self.assertIsNone(re.search(r"(?m)^\s+default\s+y\b", blocks[symbol]), symbol)
            self.assertRegex(blocks[symbol], r"Experimental", symbol)
            self.assertRegex(blocks[symbol], r"(?m)^\s+bool\s+\"", symbol)

    def test_each_symbol_depends_on_what_it_needs_and_selects_what_it_links(self):
        blocks = kconfig_blocks()
        for symbol, (needs, selects) in SLICE3_VISIBLE.items():
            self.assertRegex(blocks[symbol], rf"depends on [^\n]*\b{needs}\b", symbol)
            if needs != "CRUX_NETWORK_SETTINGS":  # that symbol reaches the module through its dependency
                self.assertRegex(blocks[symbol], r"depends on [^\n]*\bCRUX_MODULE\b", symbol)
            for chosen in selects:
                self.assertRegex(blocks[symbol], rf"(?m)^\s+select\s+{chosen}\b", f"{symbol} {chosen}")

    def test_the_network_settings_service_says_the_apply_port_is_the_applications(self):
        blocks = kconfig_blocks()
        self.assertRegex(blocks["CRUX_NETWORK_SETTINGS"], r"apply port")
        self.assertRegex(blocks["CRUX_NETWORK_SETTINGS_NO_APPLY"], r"applies nothing|no-op")

    def test_no_wolf_symbol_is_named(self):
        blocks = kconfig_blocks()
        for symbol in SLICE3_VISIBLE:
            self.assertNotRegex(blocks[symbol].upper(), r"WOLF", symbol)


class Slice3CMakeTest(unittest.TestCase):
    def test_each_directory_is_added_once_and_only_under_its_symbol(self):
        text = MODULE_CMAKE.read_text()
        for symbol, (component, _skipped, _extra) in SLICE3_DIRECTORIES.items():
            guarded = re.search(rf"if\(CONFIG_{symbol}\)(.*?)\nendif\(\)", text, re.DOTALL)
            self.assertIsNotNone(guarded, f"no if(CONFIG_{symbol}) block")
            path = f"components/{component}/ports/zephyr"
            self.assertEqual(text.count(f'/../{path}"'), 1, path)
            self.assertIn(f'/../{path}"', guarded.group(1), path)

    def test_the_module_compiles_the_host_sources_plus_the_listed_providers(self):
        for symbol, (component, skipped, extra) in SLICE3_DIRECTORIES.items():
            expected = (host_c_files(component) - skipped) | extra
            self.assertEqual(listed_sources(component, "ports/zephyr"), expected, symbol)

    def test_the_no_op_apply_port_is_built_only_under_its_symbol(self):
        text = (REPO / "components/network_settings/ports/zephyr/CMakeLists.txt").read_text()
        text = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
        head, _, tail = text.partition("if(CONFIG_CRUX_NETWORK_SETTINGS_NO_APPLY)")
        self.assertTrue(tail, "no if(CONFIG_CRUX_NETWORK_SETTINGS_NO_APPLY) block")
        guarded, _, after = tail.partition("\nendif()")
        self.assertNotIn("network_settings_no_apply.c", head)
        self.assertIn("network_settings_no_apply.c", guarded)
        self.assertNotIn("network_settings_no_apply.c", after)

    def test_reboot_is_refused_without_system(self):
        text = MODULE_CMAKE.read_text()
        guarded = re.search(r"if\(CONFIG_CRUX_REBOOT\)(.*?)\nendif\(\)", text, re.DOTALL).group(1)
        self.assertRegex(guarded, r"if\(NOT TARGET crux::system\)\s*message\(FATAL_ERROR")

    def test_no_new_directory_names_a_wolf_target_or_a_crux_alias(self):
        for symbol, (component, _skipped, _extra) in SLICE3_DIRECTORIES.items():
            text = (REPO / "components" / component / "ports/zephyr/CMakeLists.txt").read_text()
            if symbol == "CRUX_TLS_CERTIFICATE":
                # Phase 1a (ADR-0004) publishes this one target here, and no other alias.
                # tests/build/test_tls_certificate_public.py holds its definition.
                text = text.replace("add_library(crux::tls_certificate ALIAS crux_tls_certificate)", "")
            self.assertNotRegex(text.upper(), r"WOLF", symbol)
            self.assertNotRegex(without_phase_1a_aliases(text, component), r"add_library\(crux::", symbol)


# Slice 4: the web session and web management cores. The route sources stay the application's: it names
# CRUX_WEB_SESSION_ROUTES and CRUX_WEB_MANAGEMENT_ROUTES to crux_web_routes() as before.
SLICE4_VISIBLE = {
    "CRUX_WEB_SESSION": ("CRUX_WEB", {"CRUX_THREAD", "CRUX_SYNC"}),
    "CRUX_WEB_MANAGEMENT": ("CRUX_WEB_SESSION", {"CRUX_THREAD", "CRUX_SYNC"}),
}


def cmake_list_sources(path, variable):
    """Return the .c names of one set(VARIABLE ...) of a sources.cmake, relative to its directory."""
    text = (REPO / path).read_text()
    body = re.search(rf"set\({variable}\s+(.*?)CACHE", text, re.DOTALL).group(1)
    return set(re.findall(r"CMAKE_CURRENT_LIST_DIR\}/([\w./-]+\.c)", body))


class Slice4Test(unittest.TestCase):
    def test_the_symbols_exist_are_off_by_default_and_say_experimental(self):
        blocks = kconfig_blocks()
        for symbol in SLICE4_VISIBLE:
            self.assertTrue(symbol in blocks, symbol)
            self.assertIsNone(re.search(r"(?m)^\s+default\s+y\b", blocks[symbol]), symbol)
            self.assertRegex(blocks[symbol], r"Experimental", symbol)
            self.assertRegex(blocks[symbol], r"(?m)^\s+bool\s+\"", symbol)

    def test_each_symbol_depends_on_what_it_needs_and_selects_what_it_links(self):
        blocks = kconfig_blocks()
        for symbol, (needs, selects) in SLICE4_VISIBLE.items():
            self.assertRegex(blocks[symbol], rf"depends on [^\n]*\b{needs}\b", symbol)
            self.assertRegex(blocks[symbol], r"depends on [^\n]*\bCRUX_ACCESS\b", symbol)
            for chosen in selects:
                self.assertRegex(blocks[symbol], rf"(?m)^\s+select\s+{chosen}\b", f"{symbol} {chosen}")
        self.assertRegex(blocks["CRUX_WEB_MANAGEMENT"], r"depends on [^\n]*\bCRUX_CONFIG_SERVICE\b")
        self.assertRegex(blocks["CRUX_WEB_MANAGEMENT"], r"depends on [^\n]*\bCRUX_MGMT_STATUS\b")

    def test_the_routes_are_said_to_stay_the_applications(self):
        blocks = kconfig_blocks()
        for symbol in SLICE4_VISIBLE:
            self.assertRegex(blocks[symbol], r"crux_web_routes", symbol)

    def test_each_directory_is_added_once_and_only_under_its_symbol(self):
        text = MODULE_CMAKE.read_text()
        for symbol, directory in (("CRUX_WEB_SESSION", "session"), ("CRUX_WEB_MANAGEMENT", "management")):
            guarded = re.search(rf"if\(CONFIG_{symbol}\)(.*?)\nendif\(\)", text, re.DOTALL)
            self.assertIsNotNone(guarded, symbol)
            path = f"components/web/ports/zephyr/{directory}"
            self.assertEqual(text.count(f'/../{path}"'), 1, path)
            self.assertIn(f'/../{path}"', guarded.group(1), path)

    def test_the_session_library_compiles_the_session_sources_and_the_psa_port(self):
        expected = {f"../../../session/{name}" for name in cmake_list_sources(
            "components/web/session/sources.cmake", "CRUX_WEB_SESSION_SOURCES")}
        expected.add("../../psa/web_session_psa.c")
        # The provider of the event port is not in the Host list, which compiles a fake port instead.
        expected.add("../../../session/web_session_security_events.c")
        self.assertEqual(sources_in("components/web/ports/zephyr/session"), expected)

    def test_the_management_library_compiles_the_management_sources(self):
        names = cmake_list_sources("components/web/management/sources.cmake", "CRUX_WEB_MANAGEMENT_SOURCES")
        names |= cmake_list_sources("components/web/management/sources.cmake",
                                    "CRUX_WEB_MANAGEMENT_NETWORK_SOURCES")
        self.assertEqual(sources_in("components/web/ports/zephyr/management"),
                         {f"../../../management/{name}" for name in names})

    def test_the_management_network_source_is_built_only_with_network_settings(self):
        text = (REPO / "components/web/ports/zephyr/management/CMakeLists.txt").read_text()
        text = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
        head, _, tail = text.partition("if(CONFIG_CRUX_NETWORK_SETTINGS)")
        self.assertTrue(tail)
        guarded, _, after = tail.partition("\nendif()")
        self.assertNotIn("web_management_network_json.c", head)
        self.assertIn("web_management_network_json.c", guarded)
        self.assertNotIn("web_management_network_json.c", after)

    def test_no_route_source_is_compiled_into_a_library(self):
        for directory in ("session", "management"):
            for name in sources_in(f"components/web/ports/zephyr/{directory}"):
                self.assertNotRegex(name, r"_routes\.c$|web_management_network\.c$|web_management_reboot\.c$")

    def test_the_service_slots_grow_with_the_management_services_and_not_without_them(self):
        blocks = kconfig_blocks()
        for symbol, grown, plain in (("CRUX_SYNC_MUTEX_COUNT", "24", "8"), ("CRUX_THREAD_COUNT", "8", "4")):
            defaults = re.findall(r"(?m)^\s+default\s+(\d+)(?:\s+if\s+(.+))?$", blocks[symbol])
            self.assertEqual(defaults, [(grown, "CRUX_ACCESS"), (plain, "")], symbol)

    def test_the_login_worker_gets_the_stack_that_every_image_that_runs_it_sets(self):
        defaults = re.findall(r"(?m)^\s+default\s+(\d+)(?:\s+if\s+(.+))?$", kconfig_blocks()["CRUX_THREAD_STACK_BYTES"])
        # Phase 1b: the serial console thread verifies a login with the same derivation, so its symbol joins.
        self.assertEqual(defaults, [("8192", "CRUX_WEB_SESSION || CRUX_CLI_CONSOLE"), ("4096", "")])

    def test_the_management_library_links_what_provides_the_headers_it_includes(self):
        # management_status.h includes <crux/system.h>, which only crux::system puts on the include path.
        text = (REPO / "components/web/ports/zephyr/management/CMakeLists.txt").read_text()
        text = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
        links = re.search(r"target_link_libraries\(crux_web_management_zephyr PRIVATE([^)]*)\)", text).group(1)
        for target in ("crux_web_api", "crux::base", "crux::sync", "crux::system", "crux::thread"):
            self.assertIn(target, links.split(), target)
        management_status = (REPO / "components/mgmt_status/include/crux/management_status.h").read_text()
        self.assertIn("#include <crux/system.h>", management_status)

    def test_no_wolf_named_in_the_new_directories(self):
        # Phase 1a (ADR-0004) gives each directory one portable target of its own, which
        # tests/build/test_web_lifecycle_boundary.py holds to the real guards. Any other alias is refused.
        own_alias = {"session": "add_library(crux::web_session ALIAS crux_web_session)",
                     "management": "add_library(crux::web_management ALIAS crux_web_management)"}
        for directory in ("session", "management"):
            text = (REPO / f"components/web/ports/zephyr/{directory}/CMakeLists.txt").read_text()
            self.assertNotRegex(text.upper(), r"WOLF")
            self.assertIn(own_alias[directory], text)
            self.assertNotRegex(text.replace(own_alias[directory], ""), r"add_library\(crux::")


def sources_in(directory):
    """Return the .c paths a port directory lists, exactly as written."""
    text = (REPO / directory / "CMakeLists.txt").read_text()
    return set(re.findall(r"^\s*([\w./-]+\.c)\s*\)?\s*$", text, re.MULTILINE))


if __name__ == "__main__":
    unittest.main()
