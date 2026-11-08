"""The first account's password file of the RT1060-EVKB bench image, and the scan for its plaintext (C4, D3).

tests/zephyr/management_rt1060_compile/bench_first_account_file.py holds three things: the rules for the untracked
password file, a generator that never prints the password, and a scan that fails when the password or an encoding
of it is anywhere a bench build must not leave it. These tests give it files and trees to refuse and to find.

Every password here is made at run time, kept in memory or in a temporary folder outside the repository, and
never printed. A failure message names a path and a kind, and no test shows what it searched for.
"""
import base64
import json
import os
import re
import secrets
import stat
import subprocess
import sys
import tempfile
import unittest
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tests/zephyr/management_rt1060_compile"
sys.path.insert(0, str(BENCH))
import bench_first_account_file as tool  # noqa: E402

ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789"


def fresh_password(length=24):
    return "".join(secrets.choice(ALPHABET) for _ in range(length))


class Scratch(unittest.TestCase):
    """A temporary folder outside any git work tree, with a password file in it."""

    def setUp(self):
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.dir = Path(os.path.realpath(self._directory.name))
        self.password = fresh_password()
        self.file = self.write_file("first-account", self.password + "\n")

    def write_file(self, name, text, mode=0o600):
        path = self.dir / name
        data = text if isinstance(text, bytes) else text.encode("latin-1")
        path.write_bytes(data)
        path.chmod(mode)
        return path


class FileRules(Scratch):
    def refused(self, path, fragment, **kwargs):
        with self.assertRaises(tool.FileRefused) as caught:
            tool.validate(path, **kwargs)
        message = str(caught.exception)
        self.assertIn(fragment, message)
        # A refusal says what is wrong with the file, and never what is in it.
        self.assertNotIn(self.password, message)
        return message

    def test_a_good_file_is_accepted_and_only_its_hash_is_returned(self):
        found = tool.validate(self.file)
        self.assertEqual(set(found), {"sha256"})
        self.assertEqual(found["sha256"], __import__("hashlib").sha256(self.file.read_bytes()).hexdigest())

    def test_the_trailing_newline_is_optional_and_part_of_the_hash(self):
        without = self.write_file("no-newline", self.password)
        self.assertNotEqual(tool.validate(without)["sha256"], tool.validate(self.file)["sha256"])

    def test_a_missing_file_is_refused(self):
        self.refused(self.dir / "nothing", "does not exist")

    def test_no_path_at_all_is_refused(self):
        with self.assertRaises(tool.FileRefused):
            tool.validate("")

    def test_a_directory_is_refused(self):
        self.refused(self.dir, "not a regular file")

    def test_an_empty_file_is_refused(self):
        self.refused(self.write_file("empty", ""), "empty")

    def test_a_file_with_only_a_newline_is_refused(self):
        self.refused(self.write_file("newline", "\n"), "too short")

    def test_too_short_is_refused_at_the_edge(self):
        self.refused(self.write_file("short", fresh_password(tool.LENGTH_MIN - 1) + "\n"), "too short")
        tool.validate(self.write_file("edge", fresh_password(tool.LENGTH_MIN) + "\n"))

    def test_too_long_is_refused_at_the_edge(self):
        tool.validate(self.write_file("edge", fresh_password(tool.LENGTH_MAX) + "\n"))
        self.refused(self.write_file("long", fresh_password(tool.LENGTH_MAX + 1) + "\n"), "too long")

    def test_a_huge_file_is_refused_before_it_is_read(self):
        self.refused(self.write_file("huge", "a" * 100000), "too large")

    def test_more_than_one_line_is_refused(self):
        self.refused(self.write_file("two", self.password + "\n" + self.password + "\n"), "one line")

    def test_a_carriage_return_is_refused(self):
        self.refused(self.write_file("crlf", self.password + "\r\n"), "one line")

    def test_a_space_a_tab_and_a_control_byte_are_refused(self):
        for index, bad in enumerate((" ", "\t", "\x01", "\x7f")):
            text = self.password[:10] + bad + self.password[10:]
            self.refused(self.write_file(f"bad{index}", text + "\n"), "printable ASCII")

    def test_a_byte_above_ascii_is_refused(self):
        self.refused(self.write_file("high", self.password[:10].encode() + b"\xc3\xa9" + self.password[10:].encode()),
                     "printable ASCII")

    def test_a_nul_byte_is_refused(self):
        self.refused(self.write_file("nul", self.password.encode() + b"\x00" + b"x" * 4), "printable ASCII")

    def test_a_password_of_one_repeated_character_is_refused(self):
        self.refused(self.write_file("same", "a" * 24 + "\n"), "distinct")

    def test_the_public_test_password_is_refused(self):
        header = (ROOT / "tests/native/management_composition/composition/crux_native_management.h").read_text()
        public = re.search(r'CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD "([^"]+)"', header).group(1)
        self.assertGreaterEqual(len(public), tool.LENGTH_MIN)  # so that only its being public refuses it
        message = self.refused(self.write_file("public", public + "\n"), "public in this repository")
        self.assertNotIn(public, message)

    def test_the_hash_of_the_public_test_password_is_what_the_tool_holds(self):
        header = (ROOT / "tests/native/management_composition/composition/crux_native_management.h").read_text()
        public = re.search(r'CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD "([^"]+)"', header).group(1)
        self.assertEqual(tool.PUBLIC_TEST_PASSWORD_SHA256, __import__("hashlib").sha256(public.encode()).hexdigest())
        # The tool's own source never holds the password, only the hash.
        self.assertNotIn(public, (BENCH / "bench_first_account_file.py").read_text())

    def test_a_file_readable_by_the_group_or_others_is_refused(self):
        for mode in (0o640, 0o604, 0o644, 0o660, 0o666):
            self.refused(self.write_file("open", self.password + "\n", mode=mode), "readable by")
        tool.validate(self.write_file("closed", self.password + "\n", mode=0o400))

    def test_a_file_inside_a_git_work_tree_is_refused(self):
        tree = self.dir / "work"
        (tree / ".git").mkdir(parents=True)
        inside = tree / "sub" / "password"
        inside.parent.mkdir()
        inside.write_text(self.password + "\n")
        inside.chmod(0o600)
        self.refused(inside, "inside a git work tree")
        # A .git file, which is what a linked worktree has, counts too.
        other = self.dir / "linked"
        other.mkdir()
        (other / ".git").write_text("gitdir: elsewhere\n")
        inside = other / "password"
        inside.write_text(self.password + "\n")
        inside.chmod(0o600)
        self.refused(inside, "inside a git work tree")

    def test_a_file_inside_a_named_repository_root_is_refused(self):
        folder = self.dir / "checkout"
        folder.mkdir()
        inside = folder / "password"
        inside.write_text(self.password + "\n")
        inside.chmod(0o600)
        self.refused(inside, "inside the repository", repo_roots=[folder])

    def test_a_symlink_into_a_work_tree_is_refused_by_where_it_points(self):
        tree = self.dir / "work"
        (tree / ".git").mkdir(parents=True)
        target = tree / "password"
        target.write_text(self.password + "\n")
        target.chmod(0o600)
        link = self.dir / "link"
        link.symlink_to(target)
        self.refused(link, "inside a git work tree")

    def test_a_path_that_could_break_out_of_the_assembler_string_is_refused(self):
        for index, name in enumerate(('quote".pw', "back\\slash.pw", "new\nline.pw", "semi;colon.pw")):
            path = self.write_file(name, self.password + "\n")
            self.refused(path, "characters that")

    def test_the_repository_root_given_by_default_is_this_checkout(self):
        self.assertEqual(tool.REPO_ROOT, ROOT)


class Generator(Scratch):
    def test_it_writes_a_valid_private_file_and_prints_no_password(self):
        target = self.dir / "made"
        found = tool.generate(target)
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)
        self.assertEqual(tool.validate(target), found)
        password = target.read_text().rstrip("\n")
        self.assertEqual(len(password), tool.GENERATED_LENGTH)
        self.assertGreaterEqual(len(set(password)), 12)

    def test_two_files_differ(self):
        one, two = self.dir / "one", self.dir / "two"
        tool.generate(one)
        tool.generate(two)
        self.assertNotEqual(one.read_bytes(), two.read_bytes())

    def test_it_never_overwrites_a_file(self):
        before = self.file.read_bytes()
        with self.assertRaises(tool.FileRefused) as caught:
            tool.generate(self.file)
        self.assertIn("already exists", str(caught.exception))
        self.assertEqual(self.file.read_bytes(), before)

    def test_it_refuses_a_place_inside_a_work_tree(self):
        tree = self.dir / "work"
        (tree / ".git").mkdir(parents=True)
        with self.assertRaises(tool.FileRefused):
            tool.generate(tree / "password")
        self.assertFalse((tree / "password").exists())

    def test_the_command_line_prints_a_hash_and_not_the_password(self):
        target = self.dir / "cli"
        result = subprocess.run([sys.executable, str(BENCH / "bench_first_account_file.py"), "generate", "--file",
                                 str(target)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        password = target.read_text().rstrip("\n")
        self.assertNotIn(password, result.stdout + result.stderr)
        self.assertIn(tool.validate(target)["sha256"], result.stdout)


class CheckCommand(Scratch):
    def run_tool(self, *args, env=None):
        return subprocess.run([sys.executable, str(BENCH / "bench_first_account_file.py"), *args],
                              capture_output=True, text=True, check=False, env=env)

    def test_check_prints_json_with_the_hash_only(self):
        result = self.run_tool("check", "--file", str(self.file))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), tool.validate(self.file))
        self.assertNotIn(self.password, result.stdout + result.stderr)

    def test_check_fails_with_a_reason_and_no_password(self):
        short = self.write_file("short", fresh_password(8) + "\n")
        result = self.run_tool("check", "--file", str(short))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("too short", result.stderr)
        self.assertEqual(result.stdout, "")


def encodings_of(password):
    """The forms a build tool or a person could leave behind, made here independently of the tool."""
    raw = password.encode()
    forms = {
        "hex": raw.hex(),
        "hex upper": raw.hex().upper(),
        "c escapes": "".join(f"\\x{b:02x}" for b in raw),
        "c array": ", ".join(f"0x{b:02x}" for b in raw),
        "percent": urllib.parse.quote(password, safe=""),
        "utf-16": password.encode("utf-16-le"),
        "json": json.dumps(password)[1:-1],
        "reversed": password[::-1],
    }
    for lead in range(3):
        pad = b"\x00" * lead
        forms[f"base64 offset {lead}"] = ("base64", lead)
    return forms


class Scan(Scratch):
    def setUp(self):
        super().setUp()
        self.build = self.dir / "build"
        (self.build / "zephyr").mkdir(parents=True)
        self.repo = self.dir / "repo"
        (self.repo / "docs" / "evidence").mkdir(parents=True)
        self.clean()

    def clean(self):
        (self.build / "build.log").write_text("[1/551] Building C object app\nmanifest written\n")
        (self.build / "zephyr" / ".config").write_text("CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE=y\n")
        (self.build / "zephyr" / "zephyr.map").write_text("crux_bench_first_account_password 0x60001000\n")
        (self.build / "bench-manifest.json").write_text('{"first_account": {"file_sha256": "00"}}\n')
        (self.repo / "docs" / "evidence" / "uart.txt").write_text("BENCH first account created: user=admin\n")
        (self.build / "zephyr" / "zephyr.elf").write_bytes(b"\x7fELF" + os.urandom(64))

    def scan(self, *extra, expect=None, allow_image=True):
        args = [sys.executable, str(BENCH / "bench_first_account_file.py"), "scan", "--password-file", str(self.file),
                "--build", str(self.build), "--repo", str(self.repo)]
        if allow_image:
            args.append("--allow-in-image")
        for item in expect or []:
            args += ["--expect-in-image", str(item)]
        result = subprocess.run(args + list(extra), capture_output=True, text=True, check=False)
        # Whatever the verdict, the output never holds the password or any form of it.
        shown = result.stdout + result.stderr
        self.assertNotIn(self.password, shown)
        self.assertNotIn(self.password[::-1], shown)
        self.assertNotIn(self.password.encode().hex(), shown)
        return result

    def plant(self, path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode())

    def assertLeak(self, result, kind, where):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(f"LEAK {kind}", result.stdout)
        self.assertIn(str(where), result.stdout)

    def test_a_clean_tree_passes(self):
        result = self.scan()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("no plaintext", result.stdout)

    def test_the_password_in_each_kind_of_output_is_a_leak(self):
        for name in ("build.log", "zephyr/zephyr.map", "zephyr/.config", "bench-manifest.json",
                     "zephyr/include/generated/autoconf.h", "build.ninja", "CMakeCache.txt",
                     "zephyr/zephyr.lst", "zephyr/zephyr.stat", "commands.txt"):
            with self.subTest(file=name):
                self.clean()
                target = self.build / name
                self.plant(target, f"line before\nvalue={self.password}\nline after\n")
                self.assertLeak(self.scan(), "raw", target)
                target.unlink()

    def test_the_password_in_an_evidence_file_or_a_uart_capture_is_a_leak(self):
        for where in (self.repo / "docs" / "evidence" / "run" / "uart.log", self.repo / "tests" / "fixtures" / "uart.txt",
                      self.repo / "README.md"):
            with self.subTest(where=where.name):
                self.plant(where, f"BENCH login {self.password}\n")
                self.assertLeak(self.scan(), "raw", where)
                where.unlink()

    def test_the_password_with_its_newline_is_found_too(self):
        target = self.build / "stdin.txt"
        self.plant(target, self.password + "\n")
        self.assertLeak(self.scan(), "raw", target)

    def test_every_encoding_is_a_leak_in_a_text_output(self):
        for name, form in encodings_of(self.password).items():
            with self.subTest(form=name):
                self.clean()
                target = self.build / "leaked.txt"
                if isinstance(form, tuple):
                    lead = form[1]
                    encoded = base64.b64encode(b"\x41" * lead + self.password.encode() + b"\x42\x43\x44").decode()
                    self.plant(target, "x" + encoded + "y")
                elif isinstance(form, bytes):
                    self.plant(target, b"x" + form + b"y")
                else:
                    self.plant(target, "x" + form + "y")
                result = self.scan()
                self.assertEqual(result.returncode, 1, f"{name}: {result.stdout}")
                self.assertIn(str(target), result.stdout)
                target.unlink()

    def test_base64url_and_a_wrapped_base64_line_are_leaks(self):
        encoded = base64.urlsafe_b64encode(b"?" + self.password.encode() + b"??").decode()
        target = self.build / "url.txt"
        self.plant(target, encoded)
        self.assertEqual(self.scan().returncode, 1)

    def test_the_path_of_the_password_file_in_the_repository_is_reported(self):
        where = self.repo / "docs" / "evidence" / "build.txt"
        self.plant(where, f"CRUX_BENCH_PASSWORD_FILE={self.file}\n")
        self.assertLeak(self.scan(), "path", where)

    def test_the_path_of_the_password_file_in_the_build_directory_is_not_a_leak(self):
        # build.ninja and compile commands name the file. That is where it is read from, and it is not the secret.
        self.plant(self.build / "build.ninja", f"build x: asm {self.file}\n")
        self.assertEqual(self.scan().returncode, 0)

    def test_the_plaintext_in_the_image_is_allowed_only_where_the_image_is(self):
        elf = self.build / "zephyr" / "zephyr.elf"
        self.plant(elf, b"\x7fELF" + os.urandom(32) + self.password.encode() + os.urandom(32))
        self.assertEqual(self.scan().returncode, 0)
        self.assertLeak(self.scan(allow_image=False), "raw", elf)

    def test_an_object_an_archive_and_a_binary_are_images_and_a_listing_is_not(self):
        for name in ("zephyr/zephyr.bin", "app/CMakeFiles/app.dir/bench.S.obj", "app/libapp.a", "zephyr/zephyr_pre0.elf"):
            with self.subTest(name=name):
                self.clean()
                self.plant(self.build / name, os.urandom(16) + self.password.encode() + os.urandom(16))
                self.assertEqual(self.scan().returncode, 0)
        self.clean()
        listing = self.build / "zephyr" / "zephyr.lst"
        self.plant(listing, os.urandom(16) + self.password.encode())
        self.assertLeak(self.scan(), "raw", listing)

    def test_an_encoding_inside_the_image_is_still_a_leak(self):
        elf = self.build / "zephyr" / "zephyr.elf"
        self.plant(elf, b"\x7fELF" + self.password.encode().hex().encode() + os.urandom(8))
        self.assertLeak(self.scan(), "hex", elf)

    def test_the_hex_file_may_hold_hex_digits_and_not_much_else(self):
        # An Intel hex file is text made of hex digits, so a run of the password's hex form can occur in it.
        hexfile = self.build / "zephyr" / "zephyr.hex"
        record = ":10000000" + self.password.encode().hex()[:32].upper() + "00\n"
        self.plant(hexfile, record + ":10001000" + self.password.encode().hex()[32:].upper() + "00\n")
        self.assertEqual(self.scan().returncode, 0)
        self.plant(hexfile, f"; {self.password}\n")
        self.assertLeak(self.scan(), "raw", hexfile)

    def test_expect_in_image_requires_the_plaintext_exactly_once(self):
        elf = self.build / "zephyr" / "zephyr.elf"
        self.plant(elf, b"\x7fELF" + os.urandom(16))
        missing = self.scan(expect=[elf])
        self.assertEqual(missing.returncode, 1)
        self.assertIn("EXPECTED", missing.stdout)
        self.plant(elf, b"\x7fELF" + os.urandom(16) + self.password.encode() + os.urandom(16))
        self.assertEqual(self.scan(expect=[elf]).returncode, 0)
        self.plant(elf, b"\x7fELF" + self.password.encode() + os.urandom(16) + self.password.encode())
        twice = self.scan(expect=[elf])
        self.assertEqual(twice.returncode, 1)
        self.assertIn("2 times", twice.stdout)

    def test_a_build_without_the_declaration_holds_it_nowhere(self):
        # No --allow-in-image: the normal image and every native image.
        for name in ("zephyr/zephyr.elf", "zephyr/zephyr.bin", "libapp.a"):
            with self.subTest(name=name):
                self.clean()
                self.plant(self.build / name, os.urandom(16) + self.password.encode())
                self.assertLeak(self.scan(allow_image=False), "raw", self.build / name)

    def test_a_missing_password_file_is_an_error_and_not_a_pass(self):
        self.file.unlink()
        result = subprocess.run([sys.executable, str(BENCH / "bench_first_account_file.py"), "scan",
                                 "--password-file", str(self.file), "--build", str(self.build)],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2)

    def test_a_missing_build_directory_is_an_error(self):
        result = subprocess.run([sys.executable, str(BENCH / "bench_first_account_file.py"), "scan",
                                 "--password-file", str(self.file), "--build", str(self.dir / "nowhere")],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2)

    def test_the_manifest_hash_is_checked_against_the_file(self):
        manifest = self.build / "bench-manifest.json"
        good = tool.validate(self.file)["sha256"]
        manifest.write_text(json.dumps({"first_account": {"file_sha256": good}}))
        self.assertEqual(self.scan("--manifest", str(manifest)).returncode, 0)
        manifest.write_text(json.dumps({"first_account": {"file_sha256": "0" * 64}}))
        result = self.scan("--manifest", str(manifest))
        self.assertEqual(result.returncode, 1)
        self.assertIn("MANIFEST", result.stdout)

    def test_a_large_tree_is_scanned_in_reasonable_time(self):
        for index in range(200):
            self.plant(self.build / "many" / f"{index}.txt", "nothing here\n" * 100)
        self.assertEqual(self.scan().returncode, 0)

    def test_the_scan_does_not_follow_a_link_out_of_the_tree(self):
        outside = self.dir / "outside"
        outside.mkdir()
        self.plant(outside / "secret.txt", self.password)
        (self.build / "link").symlink_to(outside)
        # A link to a file: the password file itself is the likeliest target, and the scan must not read through it.
        (self.build / "file-link.txt").symlink_to(self.file)
        self.assertEqual(self.scan().returncode, 0)


class IgnoreRule(unittest.TestCase):
    def test_a_copy_of_the_file_inside_the_repository_would_be_ignored(self):
        for name in ("bench-first-account.password", "anything.bench-password", "sub/dir/x.bench-password"):
            result = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "-q", name], check=False)
            self.assertEqual(result.returncode, 0, name)


if __name__ == "__main__":
    unittest.main()
