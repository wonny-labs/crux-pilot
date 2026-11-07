"""Check that a restored package cache keeps only files that match the apt index."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts/verify-apt-cache.py"
GOOD = b"package contents"
TAMPERED = b"package CONTENTS"  # Same size: apt would accept this file by name and size alone.


def uri_line(name, contents, algorithm="SHA256"):
    digest = hashlib.new(algorithm.lower(), contents).hexdigest()
    return f"'http://mirror.example/pool/{name}' {name} {len(contents)} {algorithm}:{digest}"


class VerifyAptCacheTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.uris = self.root / "uris"

    def run_script(self, *lines):
        self.uris.write_text("".join(line + "\n" for line in lines))
        return subprocess.run([sys.executable, str(SCRIPT), str(self.uris), str(self.cache)],
                              capture_output=True, text=True)

    def test_matching_package_is_kept(self):
        (self.cache / "a_1_amd64.deb").write_bytes(GOOD)
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(GOOD, (self.cache / "a_1_amd64.deb").read_bytes())

    def test_sha512_listing_is_checked(self):
        (self.cache / "a_1_amd64.deb").write_bytes(TAMPERED)
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD, "SHA512"))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertFalse((self.cache / "a_1_amd64.deb").exists())

    def test_tampered_package_of_the_same_size_is_removed(self):
        (self.cache / "a_1_amd64.deb").write_bytes(TAMPERED)
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertFalse((self.cache / "a_1_amd64.deb").exists())

    def test_file_that_is_not_listed_is_removed(self):
        (self.cache / "old_1_amd64.deb").write_bytes(GOOD)
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([], list(self.cache.iterdir()))

    def test_directory_in_the_cache_is_removed(self):
        (self.cache / "partial").mkdir()
        (self.cache / "partial" / "a_1_amd64.deb").write_bytes(TAMPERED)
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([], list(self.cache.iterdir()))

    def test_symbolic_link_is_removed_even_when_its_target_matches(self):
        target = self.root / "outside"
        target.write_bytes(GOOD)
        (self.cache / "a_1_amd64.deb").symlink_to(target)
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([], list(self.cache.iterdir()))
        self.assertTrue(target.exists())

    def test_missing_cache_directory_is_an_empty_cache(self):
        self.cache.rmdir()
        result = self.run_script(uri_line("a_1_amd64.deb", GOOD))
        self.assertEqual(0, result.returncode, result.stderr)

    def assert_refused(self, line):
        # A listing that cannot be read must stop the job, not let unchecked files through.
        (self.cache / "a_1_amd64.deb").write_bytes(TAMPERED)
        result = self.run_script(line)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("unexpected", result.stderr)

    def test_unreadable_listing_is_refused(self):
        self.assert_refused("E: something went wrong")

    def test_weak_hash_is_refused(self):
        self.assert_refused(uri_line("a_1_amd64.deb", GOOD, "MD5"))

    def test_name_that_leaves_the_cache_is_refused(self):
        self.assert_refused(uri_line("../a_1_amd64.deb", GOOD))


if __name__ == "__main__":
    unittest.main()
