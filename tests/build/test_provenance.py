"""Regression checks for source identity and stale verification manifests."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("provenance", REPO / "scripts/record-build-provenance.py")
PROVENANCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROVENANCE)


def git(directory, *arguments):
    return subprocess.check_output(["git", "-C", str(directory), *arguments], text=True).strip()


def create_repo(directory):
    directory.mkdir(parents=True)
    git(directory, "init", "-q")
    git(directory, "config", "user.name", "Build Test")
    git(directory, "config", "user.email", "build-test@example.invalid")
    (directory / "west.yml").write_text("manifest: {}\n")
    git(directory, "add", "west.yml")
    git(directory, "commit", "-qm", "initial")


class ProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        create_repo(self.repo)

    def test_only_clean_committed_sources_are_recoverable_from_git(self):
        clean = PROVENANCE.source_snapshot(self.repo)
        self.assertTrue(clean["recoverable_from_git"])
        (self.repo / "west.yml").write_text("manifest: {version: '1.2'}\n")
        dirty = PROVENANCE.source_snapshot(self.repo)
        self.assertFalse(dirty["recoverable_from_git"])
        self.assertNotEqual(clean["snapshot_sha256"], dirty["snapshot_sha256"])
        with self.assertRaisesRegex(ValueError, "inputs changed"):
            PROVENANCE.require_matching_inputs(clean, dirty)

    def test_dependency_head_and_modifications_are_checked(self):
        dependency = self.root / "dependency"
        create_repo(dependency)
        pinned = git(dependency, "rev-parse", "HEAD")
        (self.root / ".west").mkdir()
        (self.root / ".west/config").write_text("[manifest]\npath=repo\n")
        original_run = PROVENANCE.run

        def run(command, cwd, **kwargs):
            if command[:2] == ["west", "list"]:
                return subprocess.CompletedProcess(command, 0, f"dep\t{pinned}\tdependency\n", "")
            return original_run(command, cwd, **kwargs)

        with patch.object(PROVENANCE, "run", side_effect=run):
            inputs = PROVENANCE.workspace_inputs(self.repo, self.root)
            self.assertEqual(inputs["west_projects"][0]["git_commit"], pinned)
            (dependency / ".DS_Store").write_bytes(b"Finder metadata")
            PROVENANCE.workspace_inputs(self.repo, self.root)
            (dependency / "west.yml").write_text("changed\n")
            with self.assertRaisesRegex(ValueError, "must match its pinned revision"):
                PROVENANCE.workspace_inputs(self.repo, self.root)
            git(dependency, "add", "west.yml")
            git(dependency, "commit", "-qm", "changed dependency")
            with self.assertRaisesRegex(ValueError, "must match its pinned revision"):
                PROVENANCE.workspace_inputs(self.repo, self.root)

    def test_failed_debug_build_invalidates_old_manifest(self):
        workspace = self.root
        (workspace / ".west").mkdir()
        (workspace / ".west/config").write_text("[manifest]\npath=repo\n")
        (workspace / "zephyr").mkdir()
        (workspace / "zephyr/zephyr-env.sh").touch()
        output = self.repo / "build/zephyr/test_board"
        output.mkdir(parents=True)
        manifest = output / "provenance.json"
        manifest.write_text('{"stale": true}\n')
        tools = self.root / "bin"
        tools.mkdir()
        west = tools / "west"
        west.write_text("#!/bin/sh\nexit 37\n")
        west.chmod(0o755)
        environment = dict(os.environ, CRUX_WORKSPACE=str(workspace),
                           PATH=f"{tools}:{os.environ['PATH']}")
        result = subprocess.run(["bash", str(REPO / "scripts/build-debug-target.sh"), "test_board"],
                                cwd=self.repo, env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 37, result.stderr)
        self.assertFalse(manifest.exists())

    def test_failed_clean_invalidates_old_manifest(self):
        output = self.root / "build"
        output.mkdir()
        manifest = output / "provenance.json"
        manifest.write_text('{"stale": true}\n')
        cmake = self.root / "cmake"
        cmake.write_text("#!/bin/sh\nexit 37\n")
        cmake.chmod(0o755)
        task = next(task for task in json.loads((REPO / ".vscode/tasks.json").read_text())["tasks"]
                    if task["label"] == "Clean firmware")
        arguments = [*task["args"][:-1], str(output)]
        result = subprocess.run([task["command"], *arguments], capture_output=True,
                                env=dict(os.environ, PATH=f"{self.root}:{os.environ['PATH']}"))
        self.assertEqual(result.returncode, 37)
        self.assertFalse(manifest.exists())


if __name__ == "__main__":
    unittest.main()
