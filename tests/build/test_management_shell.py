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

    def test_a_symbol_set_to_n_does_not_count(self):
        settings = RULE["read_config"]("CONFIG_CRUX_CLI=y\nCONFIG_SHELL=n\n"
                                       "CONFIG_SHELL_BACKEND_DUMMY=n\nCONFIG_MCUMGR_GRP_SHELL=n\n")
        self.assertEqual(RULE["config_errors"](settings), [])
        settings = RULE["read_config"]("CONFIG_CRUX_CLI=n\nCONFIG_SHELL=y\n")
        self.assertEqual(RULE["config_errors"](settings), [])

    def test_an_empty_config_is_refused(self):
        for text in ("", "\n", "# Nothing was set\n# CONFIG_SHELL is not set\n"):
            with self.subTest(text=text):
                errors = RULE["config_errors"](RULE["read_config"](text))
                self.assertTrue(any("no settings" in error for error in errors), errors)

    def test_the_command_reports_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".config"
            for symbols, code in ((["CRUX_CLI"], 0), (["CRUX_CLI", "SHELL"], 1)):
                config.write_text(dot_config(symbols), encoding="utf-8")
                result = subprocess.run(["python3", str(REPO / "scripts/check-management-shell.py"),
                                         "--config", str(config)], check=False, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode, code, result.stderr)
            config.write_text("", encoding="utf-8")
            result = subprocess.run(["python3", str(REPO / "scripts/check-management-shell.py"),
                                     "--config", str(config)], check=False, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(result.returncode, 1, result.stderr)
            missing = Path(directory) / "missing"
            result = subprocess.run(["python3", str(REPO / "scripts/check-management-shell.py"),
                                     "--config", str(missing)], check=False, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertNotIn("Traceback", result.stderr)


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

    def test_inspecting_nothing_fails(self):
        """A stripped image or an empty listing proves nothing, so it must not pass."""
        for text in ("", "\n", "60002000 T main\n"):
            with self.subTest(text=text):
                errors = self.errors(text)
                self.assertEqual(len([e for e in errors if "marker" in e]), 8, errors)

    def test_a_missing_marker_fails(self):
        for section in ("shell", "shell_root_cmds", "shell_subcmds", "shell_dynamic_subcmds"):
            for end in ("start", "end"):
                marker = f"_{section}_list_{end}"
                with self.subTest(marker=marker):
                    text = "".join(line + "\n" for line in NM_CLEAN.splitlines()
                                   if not line.endswith(f" {marker}"))
                    errors = self.errors(text)
                    self.assertEqual(len(errors), 1, errors)
                    self.assertIn(marker, errors[0])

    def test_undefined_symbols_are_not_linked_and_are_not_markers(self):
        """A marker that is only undefined has no address, so it is missing."""
        for kind in ("U", "w"):
            with self.subTest(kind=kind):
                # A reference to a shell symbol is not the symbol, because nm gives it no address.
                self.assertEqual(self.errors(NM_CLEAN + f"         {kind} shell_execute_cmd\n"), [])
                text = NM_CLEAN.replace("60003000 R _shell_list_end",
                                        f"         {kind} _shell_list_end")
                errors = self.errors(text)
                self.assertEqual(len(errors), 1, errors)
                self.assertIn("_shell_list_end", errors[0])

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


def run_check(*arguments):
    return subprocess.run(["python3", str(REPO / "scripts/check-management-shell.py"), *arguments],
                          check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


class NmFailure(unittest.TestCase):
    """nm is what the image check rests on, so its failure must fail the check."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.config = self.root / ".config"
        self.config.write_text(dot_config(["CRUX_CLI"]), encoding="utf-8")
        self.elf = self.root / "image.elf"
        self.elf.write_text("not looked at by these fake tools", encoding="utf-8")

    def fake_nm(self, body):
        path = self.root / "fake-nm"
        path.write_text("#!/bin/sh\n" + body + "\n", encoding="utf-8")
        path.chmod(0o755)
        return str(path)

    def test_a_failing_nm_fails(self):
        # It prints a clean listing and then fails, which must still count as a failure.
        listing = self.root / "listing.txt"
        listing.write_text(NM_CLEAN, encoding="utf-8")
        nm = self.fake_nm(f'cat "{listing}"\necho unreadable >&2\nexit 1')
        result = run_check("--config", str(self.config), "--elf", str(self.elf), "--nm", nm)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("nm could not read", result.stdout)

    def test_a_missing_nm_fails(self):
        result = run_check("--config", str(self.config), "--elf", str(self.elf),
                           "--nm", str(self.root / "no-such-nm"))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("nm could not read", result.stdout)
        self.assertNotIn("Traceback", result.stdout)

    def test_a_clean_nm_passes(self):
        listing = self.root / "listing.txt"
        listing.write_text(NM_CLEAN, encoding="utf-8")
        nm = self.fake_nm(f'cat "{listing}"')
        result = run_check("--config", str(self.config), "--elf", str(self.elf), "--nm", nm)
        self.assertEqual(result.returncode, 0, result.stdout)


# The shell's section markers, at one address, as the Zephyr linker script places them when empty.
MARKERS = "".join(f".globl _{section}_list_{end}\\n_{section}_list_{end}:\\n"
                  for section in ("shell", "shell_root_cmds", "shell_subcmds",
                                  "shell_dynamic_subcmds")
                  for end in ("start", "end"))


@unittest.skipUnless(all(shutil.which(tool) for tool in ("cc", "nm", "strip")),
                     "requires a C compiler and binutils")
class RealImage(unittest.TestCase):
    """A real ELF, through the real nm, including the stripped image that used to pass."""

    def build(self, directory, extra=""):
        source = Path(directory) / "image.c"
        source.write_text(f'__asm__(".data\\n{MARKERS}");\n{extra}\nint main(void) {{ return 0; }}\n',
                          encoding="utf-8")
        image = Path(directory) / "image.elf"
        subprocess.run(["cc", "-o", str(image), str(source)], check=True)
        return image

    def test_real_images(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".config"
            config.write_text(dot_config(["CRUX_CLI"]), encoding="utf-8")
            clean = self.build(directory)
            self.assertEqual(run_check("--config", str(config), "--elf", str(clean)).returncode, 0)
            subprocess.run(["strip", "-s", str(clean)], check=True)
            stripped = run_check("--config", str(config), "--elf", str(clean))
            self.assertEqual(stripped.returncode, 1, stripped.stdout)
            self.assertIn("marker", stripped.stdout)
            shelled = self.build(directory, "void shell_start(void); void shell_start(void) {}")
            result = run_check("--config", str(config), "--elf", str(shelled))
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("shell_start", result.stdout)


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
    """Runs the command, because its exit status is what CI reads."""

    def run_on(self, source, reviewed=""):
        """The check's result for one source file and one reviewed list."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "components/cli").mkdir(parents=True)
            (root / "samples").mkdir()
            (root / "components/cli/session-only-commands.txt").write_text(reviewed, encoding="utf-8")
            (root / "components/one.c").write_text(source, encoding="utf-8")
            return subprocess.run(["python3", str(REPO / "scripts/check-cli-session-commands.py"),
                                   "--root", str(root)], check=False, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def expect_refused(self, source, words, reviewed=""):
        result = self.run_on(source, reviewed)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(words, result.stdout)

    def test_the_repository_matches_its_list(self):
        result = subprocess.run(["python3", str(REPO / "scripts/check-cli-session-commands.py")],
                                check=False, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)
        for name in ("help", "logout", "passwd"):
            self.assertIn(f"{name}: components/cli/src/cli_commands.c", result.stdout)

    def test_a_reviewed_command_passes(self):
        source = 'CRUX_CLI_COMMAND("open", "any", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n'
        result = self.run_on(source, "# Reviewed\nopen\n")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("open: components/one.c", result.stdout)

    def test_an_unreviewed_command_fails(self):
        source = 'CRUX_CLI_COMMAND("open", "any", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n'
        self.expect_refused(source, "not reviewed: open")

    def test_a_reviewed_name_that_is_gone_fails(self):
        self.expect_refused("int unrelated;\n", "does not exist: gone", "gone\n")

    def test_a_permission_command_needs_no_review(self):
        source = ('CRUX_CLI_COMMAND("guarded", "any", CRUX_CLI_PERMISSION("x.y"), 0, 0, 1, f, NULL),\n'
                  'CRUX_CLI_COMMAND("other", "any", CRUX_CLI_PERMISSION(PERMISSION_NAME), 0, 0, 1, f,\n'
                  '                 NULL),\n')
        self.assertEqual(self.run_on(source).returncode, 0)

    def test_escaped_quotes_are_not_the_end_of_a_string(self):
        source = 'CRUX_CLI_COMMAND("esc", "say \\"hi\\"", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n'
        self.expect_refused(source, "not reviewed: esc")
        self.assertEqual(self.run_on(source, "esc\n").returncode, 0)

    def test_a_comma_or_a_parenthesis_in_a_string_is_not_syntax(self):
        source = 'CRUX_CLI_COMMAND("odd", "a, (b", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n'
        self.expect_refused(source, "not reviewed: odd")

    def test_several_lines_are_one_call(self):
        source = 'CRUX_CLI_COMMAND(\n    "split",\n    "any",\n    CRUX_CLI_SESSION_ONLY,\n    0, 0, 1, f, NULL),\n'
        self.expect_refused(source, "not reviewed: split")

    def test_a_macro_as_the_name_fails(self):
        source = 'CRUX_CLI_COMMAND(NAME, "any", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n'
        self.expect_refused(source, "cannot parse")

    def test_an_alias_of_session_only_fails(self):
        source = ('#define OPEN CRUX_CLI_SESSION_ONLY\n'
                  'CRUX_CLI_COMMAND("open", "any", OPEN, 0, 0, 1, f, NULL),\n')
        self.expect_refused(source, "cannot parse")
        self.expect_refused("#define OPEN CRUX_CLI_SESSION_ONLY\n", "outside a command")

    def test_a_raw_table_entry_fails(self):
        source = '{"open", "any", f, NULL, NULL, CRUX_CLI_ACCESS_SESSION, 0, 0, 1},\n'
        self.expect_refused(source, "CRUX_CLI_ACCESS_SESSION")

    def test_a_wrapper_macro_fails(self):
        source = ('#define COMMAND(name) CRUX_CLI_COMMAND(name, "s", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL)\n')
        self.expect_refused(source, "cannot parse")

    def test_a_call_with_too_few_arguments_fails(self):
        self.expect_refused('CRUX_CLI_COMMAND("short", "any", CRUX_CLI_SESSION_ONLY),\n', "cannot parse")

    def test_an_unterminated_call_fails(self):
        self.expect_refused('CRUX_CLI_COMMAND("open", "any", CRUX_CLI_SESSION_ONLY, 0, 0,\n', "cannot parse")

    def test_comments_and_strings_are_not_code(self):
        source = ('/* CRUX_CLI_COMMAND(NAME, CRUX_CLI_ACCESS_SESSION) */\n'
                  '// CRUX_CLI_SESSION_ONLY and CRUX_CLI_COMMAND(\n'
                  'const char *text = "CRUX_CLI_COMMAND( CRUX_CLI_ACCESS_SESSION";\n'
                  "char quote = '\"';\n")
        result = self.run_on(source)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_a_cpp_source_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "components/cli").mkdir(parents=True)
            (root / "samples/app").mkdir(parents=True)
            (root / "components/cli/session-only-commands.txt").write_text("", encoding="utf-8")
            (root / "samples/app/commands.cpp").write_text(
                'CRUX_CLI_COMMAND("open", "any", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, nullptr),\n',
                encoding="utf-8")
            result = subprocess.run(["python3", str(REPO / "scripts/check-cli-session-commands.py"),
                                     "--root", str(root)], check=False, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 1, result.stdout)

    def test_a_template_source_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "components/cli").mkdir(parents=True)
            (root / "templates/product/app/src").mkdir(parents=True)
            (root / "components/cli/session-only-commands.txt").write_text("", encoding="utf-8")
            (root / "templates/product/app/src/commands.c").write_text(
                'CRUX_CLI_COMMAND("open", "any", CRUX_CLI_SESSION_ONLY, 0, 0, 1, f, NULL),\n',
                encoding="utf-8")
            result = subprocess.run(["python3", str(REPO / "scripts/check-cli-session-commands.py"),
                                     "--root", str(root)], check=False, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("session-only command not reviewed: open", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
