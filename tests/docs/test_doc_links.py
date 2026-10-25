"""Regression tests for maintained-document selection and the real lychee CLI."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts"))
import check_docs as docs  # noqa: E402


def rule(path, reason):
    return {"path": path, "reason": reason}


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.paths = [
            "README.md",
            "docs/architecture.md",
            "docs/evidence/old.md",
            "docs/history/old.md",
            "docs/superpowers/plans/old.md",
            "docs/superpowers/specs/approved.md",
        ]
        self.policy = {
            "exclude": [
                rule("docs/evidence/", "Frozen results"),
                rule("docs/history/", "Historical"),
                rule("docs/superpowers/plans/", "Execution records"),
            ],
            "include": [],
        }

    def test_current_docs_and_approved_specs_are_included(self):
        selected, excluded = docs.select_paths(self.paths, self.policy)
        self.assertEqual(selected, [
            "README.md", "docs/architecture.md",
            "docs/superpowers/specs/approved.md",
        ])
        self.assertEqual(len(excluded), 3)

    def test_explicit_include_can_override_excluded_directory(self):
        self.policy["include"] = [rule("docs/evidence/old.md", "Maintained index")]
        selected, excluded = docs.select_paths(self.paths, self.policy)
        self.assertIn("docs/evidence/old.md", selected)
        self.assertNotIn("docs/evidence/old.md", excluded)

    def test_missing_or_stale_policy_does_not_silently_pass(self):
        with self.assertRaises(ValueError):
            docs.select_paths(self.paths, {"exclude": [rule("docs/missing/", "Stale")]})
        with self.assertRaises(ValueError):
            docs.select_paths(self.paths, {"exclude": [rule("../outside/", "Unsafe")]})
        with self.assertRaises(ValueError):
            docs.select_paths(["README.md"], {"exclude": [rule("README.md", "Excluded")]})

    def test_git_selected_docs_are_path_relative(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "README.md").write_text("# Root\n")
            (root / "docs").mkdir()
            (root / "docs" / "active.md").write_text("# Active\n")
            (root / "untracked.md").write_text("# Not tracked\n")
            subprocess.run(["git", "-C", str(root), "add", "README.md",
                            "docs/active.md"], check=True)
            self.assertEqual(
                docs.tracked_markdown(root), ["README.md", "docs/active.md"]
            )


class LycheeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary, version, _, _ = docs.tool_info(docs.load_policy())
        if not docs.verified_version(binary, version):
            raise unittest.SkipTest("Install pinned lychee with --install first")
        cls.binary = binary

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "docs" / "history").mkdir(parents=True)
        (self.root / "lychee.toml").write_text(docs.LYCHEE_CONFIG.read_text())
        self.current = self.root / "docs" / "current.md"
        self.historical = self.root / "docs" / "history" / "record.md"
        self.readme = self.root / "README.md"
        self.current.write_text("# Current heading\n")
        self.historical.write_text("# Results\n[old broken link](missing.md)\n")
        self.readme.write_text(
            "[Current](docs/current.md#current-heading)\n"
            "[History](docs/history/record.md#results)\n"
        )

    def check(self):
        return docs.run_lychee(self.root, self.binary, ["README.md", "docs/current.md"])

    def test_excluded_history_is_still_a_valid_link_target(self):
        # A broken link *inside* the unselected history must not fail the check.
        self.assertEqual(self.check(), 0)

    def test_changed_heading_breaks_unchanged_inbound_link(self):
        self.current.write_text("# New heading\n")
        self.assertEqual(self.check(), 2)

    def test_missing_source_file_is_reported(self):
        self.current.unlink()
        self.assertEqual(self.check(), 2)

    def test_removed_historical_anchor_is_reported(self):
        self.historical.write_text("# Other heading\n")
        self.assertEqual(self.check(), 2)

    def test_code_fence_is_not_checked(self):
        self.readme.write_text(
            self.readme.read_text() +
            "\n~~~markdown\n[example](nonexistent.md)\n~~~\n"
        )
        self.assertEqual(self.check(), 0)

    def test_duplicate_heading_anchor(self):
        self.current.write_text("## Heading\n## Heading\n")
        self.readme.write_text("[Second](docs/current.md#heading-1)\n")
        self.assertEqual(self.check(), 0)


if __name__ == "__main__":
    unittest.main()
