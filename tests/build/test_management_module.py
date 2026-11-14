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
            self.assertRegex(blocks[symbol], r"depends on [^\n]*CRUX_PILOT_MODULE", symbol)

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
    return set(re.findall(r"(?<![\w/.-])((?:src|access_store|ports/\w+)/[\w./-]+\.c)", text))


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
            self.assertNotRegex(text, r"add_library\(crux::", symbol)

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


    def test_the_mutex_default_grows_with_the_management_services_and_not_without_them(self):
        block = kconfig_blocks()["CRUX_SYNC_MUTEX_COUNT"]
        defaults = re.findall(r"(?m)^\s+default\s+(\d+)(?:\s+if\s+(.+))?$", block)
        self.assertEqual(defaults, [("16", "CRUX_ACCESS"), ("8", "")])


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
        self.assertNotIn("network_settings_no_apply.c", head)
        self.assertIn("network_settings_no_apply.c", tail)

    def test_reboot_is_refused_without_system(self):
        text = MODULE_CMAKE.read_text()
        guarded = re.search(r"if\(CONFIG_CRUX_REBOOT\)(.*?)\nendif\(\)", text, re.DOTALL).group(1)
        self.assertIn("crux::system", guarded)

    def test_no_new_directory_names_a_wolf_target_or_a_crux_alias(self):
        for symbol, (component, _skipped, _extra) in SLICE3_DIRECTORIES.items():
            text = (REPO / "components" / component / "ports/zephyr/CMakeLists.txt").read_text()
            self.assertNotRegex(text.upper(), r"WOLF", symbol)
            self.assertNotRegex(text, r"add_library\(crux::", symbol)


if __name__ == "__main__":
    unittest.main()
