"""The shell rule of the command line contract (rule L5), at each level, without Zephyr.

The same table of images goes through the .config check in scripts/check-management-shell.py and the
configuration rule in cmake/CruxManagementShell.cmake, and the compile rule in
components/cli/ports/zephyr/cli_shell_rule.c gets its own cases. tests/build/test_cli_composition.py
applies the rule to real builds.
"""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
RULE = runpy.run_path(str(REPO / "scripts/check-management-shell.py"))

# Each row is what an image sets, and whether the rule accepts it.
IMAGES = {
    # Row 1: management, no mark. No shell and no backend.
    "cli_only": (["CRUX_CLI"], True),
    "web_only": (["CRUX_WEB"], True),
    "ssh_only": (["CRUX_SSH"], True),
    "cli_with_shell": (["CRUX_CLI", "SHELL"], False),
    "web_with_shell": (["CRUX_WEB", "SHELL"], False),
    "ssh_with_shell": (["CRUX_SSH", "SHELL"], False),
    "cli_with_serial_shell": (["CRUX_CLI", "SHELL", "SHELL_BACKEND_SERIAL"], False),
    "cli_with_stray_backend": (["CRUX_CLI", "SHELL_BACKEND_SERIAL"], False),
    # Row 2: management, marked. The serial backend and its options only.
    "marked_no_shell": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE"], True),
    "marked_shell": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL"], True),
    "marked_serial": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_SERIAL",
                       "SHELL_BACKEND_SERIAL_API_INTERRUPT_DRIVEN",
                       "SHELL_BACKEND_SERIAL_INIT_PRIORITY"], True),
    "marked_dummy": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_DUMMY"], False),
    "marked_rtt": (["CRUX_WEB", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_RTT"], False),
    "marked_rpmsg": (["CRUX_SSH", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_RPMSG"], False),
    "marked_adsp": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL",
                     "SHELL_BACKEND_ADSP_MEMORY_WINDOW"], False),
    "marked_unknown": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_FUTURE"], False),
    "marked_near_serial": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL",
                            "SHELL_BACKEND_SERIALISH"], False),
    "marked_telnet": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_TELNET"], False),
    # Row 3: no management. Existing samples keep their settings.
    "plain": ([], True),
    "plain_shell": (["SHELL"], True),
    "plain_serial": (["SHELL", "SHELL_BACKEND_SERIAL"], True),
    "plain_dummy": (["SHELL", "SHELL_BACKEND_DUMMY"], True),
    "plain_rtt": (["SHELL", "SHELL_BACKEND_RTT"], True),
    "plain_mark_only": (["CRUX_DEVELOPMENT_IMAGE", "SHELL"], True),
    # In every image: the network backends and MCUmgr's shell group.
    "plain_telnet": (["SHELL", "SHELL_BACKEND_TELNET"], False),
    "plain_websocket": (["SHELL", "SHELL_BACKEND_WEBSOCKET"], False),
    "plain_mqtt": (["SHELL", "SHELL_BACKEND_MQTT"], False),
    "plain_mcumgr": (["SHELL", "SHELL_BACKEND_DUMMY", "MCUMGR_GRP_SHELL"], False),
    "marked_mcumgr": (["CRUX_CLI", "CRUX_DEVELOPMENT_IMAGE", "SHELL", "MCUMGR_GRP_SHELL"], False),
}


def dot_config(symbols):
    lines = ["# Generated for a test", "# CONFIG_LOG is not set", "CONFIG_BOARD=\"fixture\""]
    lines += [f"CONFIG_{symbol}=y" for symbol in symbols]
    return "\n".join(lines) + "\n"


class ConfigCheck(unittest.TestCase):
    def test_every_row(self):
        for name, (symbols, accepted) in IMAGES.items():
            with self.subTest(image=name):
                errors = RULE["config_errors"](RULE["read_config"](dot_config(symbols)))
                self.assertEqual(not errors, accepted, errors)

    def test_a_symbol_that_is_not_set_does_not_count(self):
        settings = RULE["read_config"]("# CONFIG_SHELL is not set\nCONFIG_CRUX_CLI=y\n"
                                       "# CONFIG_SHELL_BACKEND_TELNET is not set\n")
        self.assertEqual(RULE["config_errors"](settings), [])

    def test_the_command_reports_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".config"
            for symbols, code in ((["CRUX_CLI"], 0), (["CRUX_CLI", "SHELL"], 1)):
                config.write_text(dot_config(symbols), encoding="utf-8")
                result = subprocess.run(["python3", str(REPO / "scripts/check-management-shell.py"),
                                         "--config", str(config)], check=False, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode, code, result.stderr)


NM_CLEAN = """\
60002000 T main
60003000 R _shell_list_start
60003000 R _shell_list_end
60003000 R _shell_root_cmds_list_start
60003000 R _shell_root_cmds_list_end
60003000 R _shell_subcmds_list_start
60003000 R _shell_subcmds_list_end
60003000 R _shell_dynamic_subcmds_list_start
60003000 R _shell_dynamic_subcmds_list_end
         U not_linked_shell_thing
80001000 D crux_cli_builtin_commands
"""


class ImageCheck(unittest.TestCase):
    def errors(self, text):
        return RULE["image_errors"](RULE["read_symbols"](text))

    def test_empty_sections_pass(self):
        self.assertEqual(self.errors(NM_CLEAN), [])

    def test_entries_in_a_section_fail(self):
        for section in ("shell", "shell_root_cmds", "shell_subcmds", "shell_dynamic_subcmds"):
            with self.subTest(section=section):
                text = NM_CLEAN.replace(f"60003000 R _{section}_list_end",
                                        f"60003010 R _{section}_list_end")
                self.assertTrue(any(section in error for error in self.errors(text)))

    def test_shell_symbols_fail(self):
        for symbol in ("shell_execute_cmd", "z_shell_print", "__shell_uart_ctx", "shell", "_shell_x"):
            with self.subTest(symbol=symbol):
                self.assertTrue(self.errors(NM_CLEAN + f"60004000 T {symbol}\n"))

    def test_other_names_with_shell_in_them_pass(self):
        for symbol in ("eggshell_count", "bombshell", "my_shell_thing"):
            with self.subTest(symbol=symbol):
                self.assertEqual(self.errors(NM_CLEAN + f"60004000 T {symbol}\n"), [])

    def test_the_command_reads_saved_output(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".config"
            config.write_text(dot_config(["CRUX_CLI"]), encoding="utf-8")
            symbols = Path(directory) / "nm.txt"
            for text, code in ((NM_CLEAN, 0), (NM_CLEAN + "60004000 T shell_start\n", 1)):
                symbols.write_text(text, encoding="utf-8")
                result = subprocess.run(["python3", str(REPO / "scripts/check-management-shell.py"),
                                         "--config", str(config), "--nm-output", str(symbols)],
                                        check=False, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
                self.assertEqual(result.returncode, code, result.stderr)


@unittest.skipUnless(shutil.which("cmake"), "requires CMake")
class ConfigurationRule(unittest.TestCase):
    def test_every_row(self):
        """Runs the rule in script mode, with variables set as Zephyr's Kconfig import sets them."""
        rule = REPO / "cmake/CruxManagementShell.cmake"
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "case.cmake"
            for name, (symbols, accepted) in IMAGES.items():
                with self.subTest(image=name):
                    lines = [f"set(CONFIG_{symbol} y)" for symbol in symbols]
                    lines += [f'include("{rule}")', "crux_check_management_shell()"]
                    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
                    result = subprocess.run(["cmake", "-P", str(script)], check=False, text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    self.assertEqual(result.returncode == 0, accepted, result.stdout)


@unittest.skipUnless(shutil.which("cc"), "requires a C compiler")
class CompileRule(unittest.TestCase):
    """The file always has management, so the rows are the first two."""

    SOURCE = REPO / "components/cli/ports/zephyr/cli_shell_rule.c"
    CASES = {
        "cli_only": ([], True),
        "with_shell": (["SHELL"], False),
        "stray_backend": (["SHELL_BACKEND_SERIAL"], False),
        "marked_shell": (["CRUX_DEVELOPMENT_IMAGE", "SHELL"], True),
        "marked_serial": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_SERIAL"], True),
        "marked_telnet": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_TELNET"], False),
        "marked_websocket": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_WEBSOCKET"], False),
        "marked_mqtt": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_MQTT"], False),
        "marked_mcumgr": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "MCUMGR_GRP_SHELL"], False),
        "marked_rtt": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_RTT"], False),
        "marked_rpmsg": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_RPMSG"], False),
        "marked_dummy": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_DUMMY"], False),
        "marked_adsp": (["CRUX_DEVELOPMENT_IMAGE", "SHELL", "SHELL_BACKEND_ADSP_MEMORY_WINDOW"], False),
    }

    def test_every_row(self):
        for name, (symbols, accepted) in self.CASES.items():
            with self.subTest(image=name):
                command = ["cc", "-std=c17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                           "-fsyntax-only", *[f"-DCONFIG_{symbol}=1" for symbol in symbols],
                           str(self.SOURCE)]
                result = subprocess.run(command, check=False, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode == 0, accepted, result.stdout)


class SessionOnlyList(unittest.TestCase):
    SCRIPT = runpy.run_path(str(REPO / "scripts/check-cli-session-commands.py"))

    def test_the_repository_matches_its_list(self):
        found = self.SCRIPT["session_only_commands"](REPO)
        reviewed = self.SCRIPT["reviewed_commands"](REPO / "components/cli/session-only-commands.txt")
        self.assertEqual(set(found), reviewed)

    def test_a_new_command_is_found(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "components").mkdir()
            (root / "samples").mkdir()
            (root / "components/one.c").write_text(
                'CRUX_CLI_COMMAND("open", "any", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n'
                'CRUX_CLI_COMMAND("guarded", "any", CRUX_CLI_PERMISSION("x"), 0, 0, 1, f, NULL),\n',
                encoding="utf-8")
            self.assertEqual(set(self.SCRIPT["session_only_commands"](root)), {"open"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
