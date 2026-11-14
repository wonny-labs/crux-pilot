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


if __name__ == "__main__":
    unittest.main()
