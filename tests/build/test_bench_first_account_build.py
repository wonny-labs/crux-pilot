"""How the bench image's first account enters the build, and what the repository says about it (C4, D3).

bench_first_account.cmake runs bench_first_account_file.py at configuration and writes the assembler file that embeds the
password's bytes. This test runs its functions in CMake script mode, with each defective password file, and then
weakens the CMake file in a copy to show that these tests notice. It also holds the source, the declaration, the tool and
the header to the same limits, and keeps the embedded bytes' symbol out of every source that could print it.

Every password here is made at run time in a temporary folder outside the repository, and never printed.
"""
import hashlib
import json
import os
import re
import secrets
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tests/zephyr/management_rt1060_compile"
DECLARATION = json.loads((BENCH / "bench_declaration.json").read_text())
ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789"
PYTHON = shutil.which("python3") or "python3"


def fresh_password(length=24):
    return "".join(secrets.choice(ALPHABET) for _ in range(length))


@unittest.skipUnless(shutil.which("cmake"), "needs cmake")
class CMakeLevel(unittest.TestCase):
    def setUp(self):
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.dir = Path(os.path.realpath(self._directory.name))
        self.password = fresh_password()
        self.file = self.write("first-account", self.password + "\n")

    def write(self, name, text, mode=0o600):
        path = self.dir / name
        path.write_bytes(text if isinstance(text, bytes) else text.encode("latin-1"))
        path.chmod(mode)
        return path

    def run_cmake(self, body, env_file="unset", cmake_file=None):
        """body runs after bench_first_account.cmake is included. Returns the process."""
        script = self.dir / "run.cmake"
        script.write_text("cmake_minimum_required(VERSION 3.20.0)\n"
                          f'set(CRUX_ROOT "{ROOT}")\n'
                          f'set(Python3_EXECUTABLE "{PYTHON}")\n'
                          f'include("{cmake_file or BENCH / "bench_first_account.cmake"}")\n{body}\n')
        env = {k: v for k, v in os.environ.items() if k != "CRUX_BENCH_PASSWORD_FILE"}
        if env_file != "unset":
            env["CRUX_BENCH_PASSWORD_FILE"] = str(env_file)
        return subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=False, env=env)

    def check(self, env_file, **kwargs):
        return self.run_cmake('crux_bench_first_account_check(hash)\nmessage(STATUS "hash=${hash}")', env_file, **kwargs)

    def refused(self, env_file, fragment, **kwargs):
        result = self.check(env_file, **kwargs)
        shown = " ".join((result.stderr + result.stdout).split())
        self.assertNotEqual(result.returncode, 0, shown)
        self.assertIn(fragment, shown)
        self.assertNotIn("hash=", shown)
        self.assertNotIn(self.password, shown)
        return shown

    def test_a_good_file_gives_its_hash(self):
        result = self.check(self.file)
        self.assertEqual(result.returncode, 0, result.stderr)
        digest = hashlib.sha256(self.file.read_bytes()).hexdigest()
        self.assertIn(f"hash={digest}", result.stderr + result.stdout)
        self.assertNotIn(self.password, result.stderr + result.stdout)

    def test_no_environment_variable_is_refused_and_says_how_to_make_a_file(self):
        shown = self.refused("unset", "set CRUX_BENCH_PASSWORD_FILE")
        self.assertIn("bench_first_account_file.py", shown)

    def test_an_empty_variable_is_refused(self):
        self.refused("", "set CRUX_BENCH_PASSWORD_FILE")

    def test_a_missing_file_is_refused(self):
        self.refused(self.dir / "nothing", "does not exist")

    def test_an_empty_file_is_refused(self):
        self.refused(self.write("empty", ""), "empty")

    def test_a_too_short_file_is_refused(self):
        self.refused(self.write("short", fresh_password(15) + "\n"), "too short")

    def test_a_too_long_file_is_refused(self):
        self.refused(self.write("long", fresh_password(129) + "\n"), "too long")

    def test_a_group_readable_file_is_refused(self):
        self.refused(self.write("open", self.password + "\n", mode=0o640), "readable by")

    def test_a_file_in_a_work_tree_is_refused(self):
        tree = self.dir / "work"
        (tree / ".git").mkdir(parents=True)
        inside = tree / "password"
        inside.write_text(self.password + "\n")
        inside.chmod(0o600)
        self.refused(inside, "inside a git work tree")

    def test_a_file_in_this_repository_is_refused(self):
        # The repository named by the build is a checkout even where it has no .git entry that the check can see.
        folder = self.dir / "checkout"
        folder.mkdir()
        inside = folder / "password"
        inside.write_text(self.password + "\n")
        inside.chmod(0o600)
        script = self.dir / "run2.cmake"
        script.write_text("cmake_minimum_required(VERSION 3.20.0)\n"
                          f'set(CRUX_ROOT "{folder}")\nset(Python3_EXECUTABLE "{PYTHON}")\n'
                          f'include("{BENCH / "bench_first_account.cmake"}")\ncrux_bench_first_account_check(hash)\n')
        env = dict(os.environ, CRUX_BENCH_PASSWORD_FILE=str(inside))
        result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=False, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("inside the repository", " ".join(result.stderr.split()))

    def test_the_public_test_password_is_refused(self):
        header = (ROOT / "tests/native/management_composition/composition/crux_native_management.h").read_text()
        public = re.search(r'CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD "([^"]+)"', header).group(1)
        shown = self.refused(self.write("public", public + "\n"), "public in this repository")
        self.assertNotIn(public, shown)

    def test_the_assembler_file_names_the_file_and_holds_no_password(self):
        result = self.run_cmake(f'crux_bench_first_account_asm(text "{self.file}")\nfile(WRITE "{self.dir}/out.S" "${{text}}")',
                                self.file)
        self.assertEqual(result.returncode, 0, result.stderr)
        asm = (self.dir / "out.S").read_text()
        self.assertIn(f'.incbin "{self.file}"', asm)
        self.assertNotIn(self.password, asm)
        for name in ("crux_bench_first_account_password:", "crux_bench_first_account_password_end:",
                     "crux_bench_first_account_password_size:", ".section .rodata", ".global crux_bench_first_account_password"):
            self.assertIn(name, asm)
        self.assertEqual(asm.count(".incbin"), 1)

    def test_prepare_is_refused_without_the_form(self):
        result = self.run_cmake("crux_bench_first_account_prepare()", self.file)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("only for CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE", " ".join(result.stderr.split()))

    def test_the_function_that_records_the_hash_for_the_manifest_returns_what_prepare_stored(self):
        result = self.run_cmake('set_property(GLOBAL PROPERTY CRUX_BENCH_FIRST_ACCOUNT_SHA256 "abc")\n'
                                'crux_bench_first_account_sha256(out)\nmessage(STATUS "recorded=${out}")')
        self.assertIn("recorded=abc", result.stderr + result.stdout)

    # Each weakening of the CMake file in a copy must make some test above fail: here, the same cases run on the copy.
    def test_weakened_copies_of_the_cmake_file_are_noticed(self):
        original = (BENCH / "bench_first_account.cmake").read_text()
        weakenings = {
            "the status of the check is ignored": ("if(NOT status EQUAL 0)", "if(FALSE)"),
            "an unset variable is accepted": ('if("$ENV{CRUX_BENCH_PASSWORD_FILE}" STREQUAL "")', "if(FALSE)"),
            "the repository is not named to the check": ('--file "$ENV{CRUX_BENCH_PASSWORD_FILE}" --repo "${CRUX_ROOT}"',
                                                        '--file "$ENV{CRUX_BENCH_PASSWORD_FILE}"'),
            "the hash is not returned": ('set(${hash_var} "${hash}" PARENT_SCOPE)', 'set(${hash_var} "" PARENT_SCOPE)'),
            "a second file is embedded": ('string(APPEND text "\\t.incbin \\"${path}\\"\\n")',
                                         'string(APPEND text "\\t.incbin \\"${path}\\"\\n\\t.incbin \\"${path}\\"\\n")'),
        }
        for name, (old, new) in weakenings.items():
            with self.subTest(weakening=name):
                self.assertIn(old, original, name)
                weak = self.dir / "weak.cmake"
                weak.write_text(original.replace(old, new, 1))
                # The cases that must tell the weak copy from the real file.
                short = self.write("short", fresh_password(8) + "\n")
                noticed = []
                noticed.append(self.check("unset", cmake_file=weak).returncode == 0)
                noticed.append(self.check(short, cmake_file=weak).returncode == 0)
                noticed.append("hash=" not in self.check(self.file, cmake_file=weak).stderr)
                made = self.run_cmake(f'crux_bench_first_account_asm(text "{self.file}")\nfile(WRITE "{self.dir}/w.S" "${{text}}")',
                                      self.file, cmake_file=weak)
                noticed.append(made.returncode == 0 and (self.dir / "w.S").read_text().count(".incbin") != 1)
                folder = self.dir / "inrepo"
                folder.mkdir(exist_ok=True)
                inside = folder / "pw"
                inside.write_text(self.password + "\n")
                inside.chmod(0o600)
                script = self.dir / "run3.cmake"
                script.write_text("cmake_minimum_required(VERSION 3.20.0)\n"
                                  f'set(CRUX_ROOT "{folder}")\nset(Python3_EXECUTABLE "{PYTHON}")\n'
                                  f'include("{weak}")\ncrux_bench_first_account_check(hash)\n')
                env = dict(os.environ, CRUX_BENCH_PASSWORD_FILE=str(inside))
                noticed.append(subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=False,
                                              env=env).returncode == 0)
                self.assertTrue(any(noticed), f"no case notices: {name}")


class Agreement(unittest.TestCase):
    def test_limits_agree_between_declaration_tool_and_header(self):
        import sys
        sys.path.insert(0, str(BENCH))
        import bench_first_account_file as tool
        account = DECLARATION["first_account"]
        header = (BENCH / "first_account/bench_first_account_provision.h").read_text()
        minimum = int(re.search(r"LENGTH_MIN (\d+)u", header).group(1))
        maximum = int(re.search(r"LENGTH_MAX (\d+)u", header).group(1))
        self.assertEqual((account["password_length_min"], account["password_length_max"]), (tool.LENGTH_MIN, tool.LENGTH_MAX))
        self.assertEqual((minimum, maximum), (tool.LENGTH_MIN, tool.LENGTH_MAX))
        # Access takes 12 to 128 bytes, so the file's own minimum is the stricter one and is the one that applies.
        self.assertGreater(tool.LENGTH_MIN, 12)
        self.assertEqual(tool.LENGTH_MAX, 128)
        self.assertEqual(account["password_file_env"], tool.ENV_NAME)

    def test_the_account_is_the_administrator_of_the_native_policy(self):
        account = DECLARATION["first_account"]
        header = (ROOT / "tests/native/management_composition/composition/crux_native_management.h").read_text()
        self.assertIn(f'CRUX_NATIVE_MANAGEMENT_ROLE_ADMINISTRATOR "{account["role"]}"', header)
        self.assertIn(f'CRUX_NATIVE_MANAGEMENT_TEST_USER "{account["user"]}"', header)
        # The bench image's policy is the native one, from the copy of crux_native_management.c, and its administrator
        # role holds the permission that manages accounts.
        start = (BENCH / "src/mgmt_start.c").read_text()
        self.assertIn("CRUX_ACCESS_PERMISSION_MANAGE", start)

    def test_the_host_suite_uses_the_declared_account(self):
        account = DECLARATION["first_account"]
        suite = (ROOT / "tests/bench_first_account/first_account.c").read_text()
        self.assertIn(f'#define USER "{account["user"]}"', suite)
        self.assertIn(f'#define ROLE "{account["role"]}"', suite)

    def test_the_first_account_form_is_selected_and_set(self):
        form = DECLARATION["test_forms"]["first_account_file"]
        self.assertEqual((form["symbol"], form["state"]), ("CRUX_BENCH_FIRST_ACCOUNT_FILE", "selected"))
        self.assertIn("CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE=y", (BENCH / "target.conf").read_text())

    def test_the_embedded_bytes_are_named_only_where_they_are_defined_and_passed_on(self):
        symbol = "crux_bench_first_account_password"
        users = {}
        for path in sorted(list(BENCH.rglob("*.c")) + list(BENCH.rglob("*.h"))):
            count = path.read_text().count(symbol)
            if count:
                users[path.relative_to(BENCH).as_posix()] = count
        # The glue declares both symbols and passes them to the step once. No other source names them.
        self.assertEqual(list(users), ["src/bench_first_account.c"], users)
        glue = (BENCH / "src/bench_first_account.c").read_text()
        for line in glue.splitlines():
            if symbol in line:
                self.assertTrue(line.lstrip().startswith(("extern", "crux_bench_first_account_password")), line)
        # Nothing that prints, logs, copies or formats a password sits near them.
        body = glue[glue.index("bool crux_bench_first_account(void)"):]
        for banned in ("memcpy", "strcpy", "snprintf", "%s\\n\", crux_bench", "LOG_", "hexdump"):
            self.assertNotIn(banned, body)
        self.assertEqual(len(re.findall(r"printk\(", body)), 2)

    def test_no_bench_source_prints_the_password_buffer(self):
        for path in sorted(BENCH.rglob("*.c")):
            text = path.read_text()
            for call in re.findall(r"(?:printk|LOG_\w+|printf|snprintf)\([^;]*;", text, re.DOTALL):
                self.assertNotIn("first_account_password", call, path.name)
                self.assertNotIn("file_length", call, path.name)

    def test_the_provision_step_holds_no_password_literal_and_no_zephyr_header(self):
        text = (BENCH / "first_account/bench_first_account_provision.c").read_text()
        self.assertNotRegex("\n".join(line for line in text.splitlines() if not line.startswith("#include")),
                            r'"[A-Za-z0-9!-~]{12,}"')
        self.assertNotIn("zephyr/", text)
        self.assertNotIn("printk", text)

    def test_the_cmake_file_makes_the_password_enter_by_the_file_and_not_through_a_variable(self):
        text = (BENCH / "bench_first_account.cmake").read_text()
        # Never read into CMake: file(READ) and file(STRINGS) of the password file would put the plaintext in a variable,
        # a trace and possibly a generated file. Only the hash and the path are used.
        self.assertNotIn("file(READ", text.replace("file(READ \"${asm_file}\" existing)", ""))
        self.assertNotIn("file(STRINGS", text)
        self.assertNotIn("CACHE", text)
        self.assertIn(".incbin", text)

    def test_the_manifest_never_gets_the_password(self):
        text = (BENCH / "bench_guard.cmake").read_text()
        self.assertIn("first_account_hash", text)
        self.assertNotIn("PASSWORD_FILE}\"", text.replace("password_file_env", ""))

    def test_the_product_alternative_is_documented(self):
        report = (ROOT / "docs/rt1060-management-image-feasibility.md").read_text()
        self.assertIn("Row C4", report)


if __name__ == "__main__":
    unittest.main()
