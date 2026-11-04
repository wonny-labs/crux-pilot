"""Regression checks for source identity and stale verification manifests."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
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

    def test_failed_build_or_test_invalidates_old_manifest(self):
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
                           PATH=f"{tools}:{os.environ['PATH']}", CRUX_PYTHON="/bin/true")
        for name in ("build-debug-target.sh", "test-zephyr-time.sh"):
            with self.subTest(script=name):
                manifest.write_text('{"stale": true}\n')
                result = subprocess.run(["bash", str(REPO / "scripts" / name), "test_board"],
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


ORIGINAL_RUN = PROVENANCE.run
REQUIRED_OUTPUTS = 8
PASSING_REPORT = '<testsuites><testsuite tests="3" failures="0" errors="0" skipped="0"/></testsuites>\n'
FAILING_REPORT = '<testsuites><testsuite tests="3" failures="1" errors="0" skipped="0"/></testsuites>\n'


def config_text(location, disabled="CONFIG_UNUSED", value="1"):
    """A Kconfig output shaped like the one measured: path comments, sections and values."""
    return (f"#\n# Automatically generated file; DO NOT EDIT.\n# Zephyr Kernel Configuration\n#\n"
            f'CONFIG_BOARD="test_board"\n# {disabled} is not set\n\n'
            f"#\n# crux-pilot ({location})\n#\nCONFIG_VALUE={value}\n"
            f"# end of crux-pilot ({location})\n")


def write_build(repo, relative="build/app", location=None):
    """Fake build outputs: the image is fixed, the path shows up where the PoC measured it."""
    location = location or str(repo)
    build = repo / relative
    (build / "zephyr").mkdir(parents=True)
    (build / "zephyr/zephyr.bin").write_bytes(b"\x7fimage")
    (build / "zephyr/zephyr.elf").write_text(f"elf {location}\n")
    (build / "zephyr/.config").write_text(config_text(location))
    (build / "zephyr/zephyr.dts").write_text("/dts-v1/;\n")
    (build / "zephyr/zephyr.stat").write_text("stat\n")
    (build / "zephyr/zephyr.map").write_text(f"map {location}\n")
    (build / "rom.json").write_text("{}\n")
    (build / "ram.json").write_text("{}\n")
    (build / "CMakeCache.txt").write_text("CMAKE_C_COMPILER:FILEPATH=/nonexistent/cc\n")
    return build


def expected_build_id(manifest, build):
    """The build_id algorithm and inputs as they were before firmware_identity existed."""
    stable_image = {**manifest["image"], "github_runner": {
        key: manifest["image"]["github_runner"][key] for key in ("provider", "os", "image_version")}}
    identity = {
        "source": manifest["source"],
        "target": manifest["target"],
        "configuration_sha256": {
            name: hashlib.sha256((build / "zephyr" / name).read_bytes()).hexdigest()
            for name in (".config", "zephyr.dts")},
        "west_projects": manifest["west_projects"],
        "west_workspace_config_sha256": manifest["west_workspace_config_sha256"],
        "tools": manifest["tools"],
        "image": stable_image,
        "build_outputs": manifest["artifacts"][:REQUIRED_OUTPUTS],
    }
    return hashlib.sha256(
        json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


@contextlib.contextmanager
def inside_container():
    """Take the branch of image_identity that hashes the recipe on any host."""
    real = Path.exists

    def exists(path, *arguments, **keywords):
        return True if str(path) == "/.dockerenv" else real(path, *arguments, **keywords)

    with patch.object(Path, "exists", exists):
        yield


class ConfigNormalisationTest(unittest.TestCase):
    def test_keeps_only_lines_that_carry_meaning(self):
        text = ('#\n# plain comment\n# crux-pilot (/poc/ws/q6a/crux-pilot)\n\n'
                '# CONFIG_FOO is not set\nCONFIG_BAR=y\nCONFIG_NAME="a # b"\n'
                '# end of crux-pilot (/poc/ws/q6a/crux-pilot)\n')
        self.assertEqual(PROVENANCE.normalised_config(text.encode()),
                         b'# CONFIG_FOO is not set\nCONFIG_BAR=y\nCONFIG_NAME="a # b"\n')

    def test_comment_that_only_resembles_a_disabled_option_is_dropped(self):
        for line in (b"# CONFIG_FOO is not set by /poc/ws", b"# see CONFIG_FOO is not set",
                     b"#CONFIG_FOO is not set"):
            with self.subTest(line=line):
                self.assertEqual(PROVENANCE.normalised_config(line + b"\nCONFIG_A=1\n"), b"CONFIG_A=1\n")

    def test_configuration_hash_ignores_paths_but_not_settings(self):
        def identity(directory, text):
            (directory / ".config").write_text(text)
            return PROVENANCE.firmware_identity(directory, "board")["configuration_sha256"]

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "zephyr.bin").write_bytes(b"image")
            base = identity(directory, config_text("/a/one"))
            self.assertEqual(base, identity(directory, config_text("/somewhere/else/entirely")))
            self.assertNotEqual(base, identity(directory, config_text("/a/one", disabled="CONFIG_OTHER")))
            self.assertNotEqual(base, identity(directory, config_text("/a/one", value="2")))

    def test_digest_is_sha256_of_the_fields_in_canonical_json(self):
        fields = {"target": "board", "algorithm": "crux-firmware-identity-v1",
                  "image_sha256": "aa", "configuration_sha256": "bb"}
        self.assertEqual(PROVENANCE.firmware_digest(fields), hashlib.sha256(
            b'{"algorithm":"crux-firmware-identity-v1","configuration_sha256":"bb",'
            b'"image_sha256":"aa","target":"board"}').hexdigest())


class RecordingTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        for name in ("CRUX_WORKSPACE", "CRUX_BUILD_IMAGE_ID"):
            os.environ.pop(name, None)
        self.west_directories = []

    def make_workspace(self, name="ws", repository="product-x", dependency="crux", recipe=False):
        workspace = self.root / name
        repo = workspace / repository
        create_repo(repo)
        (repo / ".gitignore").write_text("/build/\n")
        if recipe:
            (repo / ".devcontainer").mkdir()
            (repo / ".devcontainer/Dockerfile").write_text("FROM product-base:1\n")
            (repo / ".devcontainer/devcontainer.json").write_text("{}\n")
        git(repo, "add", ".")
        git(repo, "commit", "-qm", "layout")
        create_repo(workspace / dependency)
        (workspace / ".west").mkdir()
        (workspace / ".west/config").write_text(f"[manifest]\npath={repository}\n")
        self.listing = (f"manifest\tHEAD\t{repository}\n"
                        f"{dependency}\t{git(workspace / dependency, 'rev-parse', 'HEAD')}\t{dependency}\n")
        return workspace, repo, workspace / dependency

    def invoke(self, *arguments):
        """Run main() with west stubbed. Returns (exit code, stdout, stderr)."""
        def run(command, cwd, **keywords):
            if command[:2] == ["west", "list"]:
                self.west_directories.append(Path(cwd))
                return subprocess.CompletedProcess(command, 0, self.listing, "")
            return ORIGINAL_RUN(command, cwd, **keywords)

        out, err = io.StringIO(), io.StringIO()
        with patch.object(PROVENANCE, "run", side_effect=run), \
                patch.object(sys, "argv", ["record-build-provenance.py", *map(str, arguments)]), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                code = PROVENANCE.main()
            except SystemExit as stop:
                code = stop.code
        return code, out.getvalue(), err.getvalue()

    def capture(self, *arguments, name="inputs.json"):
        inputs = self.root / name
        code, _, err = self.invoke(*arguments, "--capture-inputs", inputs)
        self.assertEqual(code, 0, err)
        return inputs

    def record(self, repo, build, *arguments, workspace=None):
        """Capture inputs and record a build of repo, then return (manifest, stdout)."""
        location = ("--workspace", workspace) if workspace else ()
        inputs = self.capture("--repo", repo, *location)
        code, out, err = self.invoke("--repo", repo, *location, "--build-dir", build,
                                     "--target", "board", "--inputs", inputs, *arguments)
        self.assertEqual(code, 0, err)
        return json.loads((build / "provenance.json").read_text()), out

    def test_product_repository_is_recorded_with_crux_as_a_dependency(self):
        _, repo, crux = self.make_workspace(recipe=True)
        build = write_build(repo)
        (repo / "build/evidence").mkdir(parents=True)
        (repo / "build/evidence/evkb-build.log").write_text("log\n")
        manifest, _ = self.record(repo, build)
        self.assertEqual(manifest["repository"], "product-x")
        self.assertEqual(manifest["build_directory"], "build/app")
        self.assertEqual(manifest["source"], PROVENANCE.source_snapshot(repo))
        self.assertEqual([project["name"] for project in manifest["west_projects"]], ["crux"])
        self.assertEqual(manifest["west_projects"][0]["git_commit"], git(crux, "rev-parse", "HEAD"))
        paths = [artifact["path"] for artifact in manifest["artifacts"]]
        self.assertIn("build/app/zephyr/zephyr.bin", paths)
        self.assertEqual(paths[REQUIRED_OUTPUTS:], ["build/evidence/evkb-build.log"])
        self.assertEqual(manifest["build_id"], expected_build_id(manifest, build))

    def test_workspace_defaults_to_the_parent_of_the_given_repository(self):
        workspace, repo, _ = self.make_workspace()
        self.capture("--repo", repo)
        self.assertEqual(self.west_directories, [workspace])

    def test_explicit_workspace_and_environment_still_win_over_the_default(self):
        workspace, repo, _ = self.make_workspace()
        moved = self.root / "elsewhere/product-x"
        moved.parent.mkdir()
        shutil.copytree(repo, moved, symlinks=True)
        self.capture("--repo", moved, "--workspace", workspace)
        os.environ["CRUX_WORKSPACE"] = str(workspace)
        self.capture("--repo", moved)
        self.assertEqual(self.west_directories, [workspace, workspace])

    def test_repository_without_a_west_manifest_is_refused_clearly(self):
        workspace, repo, _ = self.make_workspace()
        git(repo, "rm", "-q", "west.yml")
        git(repo, "commit", "-qm", "no manifest")
        code, _, err = self.invoke("--repo", repo, "--workspace", workspace, "--capture-inputs",
                                   self.root / "inputs.json")
        self.assertEqual(code, 2)
        self.assertRegex(err, "west.yml")

    def test_build_directory_outside_the_repository_is_refused_clearly(self):
        workspace, repo, _ = self.make_workspace()
        inputs = self.capture("--repo", repo)
        # The sibling shares the repository's name as a prefix, so a text prefix check would pass it.
        for outside in (workspace / "crux/build", self.root / "other", workspace / "product-x-old/build"):
            with self.subTest(outside=outside):
                outside.mkdir(parents=True)
                stale = outside / "provenance.json"
                stale.write_text('{"stale": true}\n')
                code, _, err = self.invoke("--repo", repo, "--build-dir", outside, "--target", "board",
                                           "--inputs", inputs)
                self.assertEqual(code, 2)
                self.assertRegex(err, "build directory .* outside the repository")
                self.assertTrue(stale.exists())

    def test_firmware_identity_does_not_depend_on_the_workspace_path(self):
        first, first_repo, _ = self.make_workspace(name="q6a")
        second = self.root / "somewhere/much/deeper/q9z"
        shutil.copytree(first, second, symlinks=True)
        second_repo = second / "product-x"
        results = [self.record(repo, write_build(repo)) for repo in (first_repo, second_repo)]
        (manifest_one, out_one), (manifest_two, out_two) = results
        self.assertEqual(manifest_one["source"], manifest_two["source"])
        self.assertEqual(manifest_one["firmware_identity"], manifest_two["firmware_identity"])
        self.assertNotEqual(manifest_one["build_id"], manifest_two["build_id"])
        digest = PROVENANCE.firmware_digest(manifest_one["firmware_identity"])
        for out in (out_one, out_two):
            self.assertIn(f"Firmware identity: {digest}\n", out)

    def test_firmware_identity_fields_come_from_the_build_outputs(self):
        _, repo, _ = self.make_workspace()
        build = write_build(repo)
        manifest, _ = self.record(repo, build)
        self.assertEqual(manifest["firmware_identity"], {
            "algorithm": "crux-firmware-identity-v1",
            "target": "board",
            "image_sha256": hashlib.sha256(b"\x7fimage").hexdigest(),
            "configuration_sha256": hashlib.sha256(PROVENANCE.normalised_config(
                (build / "zephyr/.config").read_bytes())).hexdigest(),
        })

    def test_changing_the_image_changes_the_firmware_identity(self):
        _, repo, _ = self.make_workspace()
        build = write_build(repo)
        before, _ = self.record(repo, build)
        (build / "zephyr/zephyr.bin").write_bytes(b"\x7fother")
        after, _ = self.record(repo, build)
        self.assertNotEqual(before["firmware_identity"], after["firmware_identity"])

    def test_firmware_identity_and_recipe_source_stay_out_of_build_id(self):
        _, repo, _ = self.make_workspace(recipe=True)
        build = write_build(repo)
        with inside_container():
            manifest, _ = self.record(repo, build)
        self.assertEqual(manifest["build_id"], expected_build_id(manifest, build))
        self.assertEqual(set(manifest["image"]), {
            "execution_environment", "devcontainer_recipe", "installed_package_inventory_sha256",
            "github_runner", "host"})
        self.assertEqual(set(manifest["image"]["devcontainer_recipe"]),
                         {"base_image", "dockerfile_sha256", "config_sha256"})

    def test_recipe_in_the_repository_is_preferred(self):
        _, repo, _ = self.make_workspace(recipe=True)
        with inside_container():
            manifest, _ = self.record(repo, write_build(repo))
        self.assertEqual(manifest["devcontainer_recipe_source"],
                         {"directory": ".devcontainer", "origin": "repository"})
        recipe = manifest["image"]["devcontainer_recipe"]
        self.assertEqual(recipe["base_image"], "product-base:1")
        self.assertEqual(recipe["dockerfile_sha256"],
                         hashlib.sha256(b"FROM product-base:1\n").hexdigest())

    def test_explicit_recipe_wins_and_is_named_without_its_location(self):
        _, repo, _ = self.make_workspace(recipe=True)
        shared = self.root / "shared/recipes/board-a"
        shared.mkdir(parents=True)
        (shared / "Dockerfile").write_text("FROM shared-base:2\n")
        (shared / "devcontainer.json").write_text("{}\n")
        with inside_container():
            manifest, _ = self.record(repo, write_build(repo), "--devcontainer", shared)
        self.assertEqual(manifest["devcontainer_recipe_source"],
                         {"directory": "board-a", "origin": "explicit"})
        self.assertEqual(manifest["image"]["devcontainer_recipe"]["base_image"], "shared-base:2")

    def test_explicit_recipe_inside_the_repository_is_shown_relative_to_it(self):
        _, repo, _ = self.make_workspace(recipe=True)
        (repo / "tools/container").mkdir(parents=True)
        (repo / "tools/container/devcontainer.json").write_text('{"image": "prebuilt"}\n')
        git(repo, "add", ".")
        git(repo, "commit", "-qm", "recipe")
        with inside_container():
            manifest, _ = self.record(repo, write_build(repo), "--devcontainer", repo / "tools/container")
        self.assertEqual(manifest["devcontainer_recipe_source"]["directory"], "tools/container")
        recipe = manifest["image"]["devcontainer_recipe"]
        self.assertIsNone(recipe["base_image"])
        self.assertIsNone(recipe["dockerfile_sha256"])
        self.assertIsNotNone(recipe["config_sha256"])

    def test_explicit_recipe_directory_that_holds_no_recipe_is_refused(self):
        _, repo, _ = self.make_workspace()
        inputs = self.capture("--repo", repo)
        empty = self.root / "empty"
        empty.mkdir()
        build = write_build(repo)
        for directory in (empty, self.root / "absent"):
            with self.subTest(directory=directory):
                code, _, err = self.invoke("--repo", repo, "--build-dir", build, "--target", "board",
                                           "--inputs", inputs, "--devcontainer", directory)
                self.assertEqual(code, 2)
                self.assertRegex(err, "--devcontainer")

    def test_product_without_a_recipe_uses_the_one_next_to_the_script(self):
        _, repo, _ = self.make_workspace()
        crux = self.root / "tools/crux"
        (crux / ".devcontainer").mkdir(parents=True)
        (crux / ".devcontainer/Dockerfile").write_text("FROM crux-base:3\n")
        (crux / ".devcontainer/devcontainer.json").write_text("{}\n")
        with patch.object(PROVENANCE, "REPO", crux), inside_container():
            manifest, _ = self.record(repo, write_build(repo))
        self.assertEqual(manifest["devcontainer_recipe_source"],
                         {"directory": ".devcontainer", "origin": "crux"})
        self.assertEqual(manifest["image"]["devcontainer_recipe"]["base_image"], "crux-base:3")

    def test_crux_as_a_project_inside_the_product_is_shown_relative_to_it(self):
        _, repo, _ = self.make_workspace()
        crux = repo / "modules/crux"
        (crux / ".devcontainer").mkdir(parents=True)
        (crux / ".devcontainer/Dockerfile").write_text("FROM crux-base:3\n")
        git(repo, "add", ".")
        git(repo, "commit", "-qm", "vendored layout")
        with patch.object(PROVENANCE, "REPO", crux):
            manifest, _ = self.record(repo, write_build(repo))
        self.assertEqual(manifest["devcontainer_recipe_source"],
                         {"directory": "modules/crux/.devcontainer", "origin": "crux"})

    def test_no_recipe_anywhere_does_not_crash_and_says_so(self):
        _, repo, _ = self.make_workspace()
        with patch.object(PROVENANCE, "REPO", self.root / "no-such-checkout"), inside_container():
            manifest, _ = self.record(repo, write_build(repo))
        self.assertEqual(manifest["devcontainer_recipe_source"], {"directory": None, "origin": None})
        self.assertIsNone(manifest["image"]["devcontainer_recipe"])

    def test_new_fields_hold_no_absolute_path_from_the_workspace(self):
        workspace, repo, _ = self.make_workspace(recipe=True)
        outside = self.root / "shared/recipe"
        outside.mkdir(parents=True)
        (outside / "Dockerfile").write_text("FROM base:1\n")
        for arguments in ((), ("--devcontainer", outside)):
            with self.subTest(arguments=arguments):
                shutil.rmtree(repo / "build", ignore_errors=True)
                with inside_container():
                    manifest, _ = self.record(repo, write_build(repo), *arguments)
                new_fields = json.dumps([manifest["firmware_identity"], manifest["devcontainer_recipe_source"]])
                self.assertNotIn(str(self.root), new_fields)
                self.assertNotIn(str(workspace), new_fields)
                self.assertFalse(manifest["devcontainer_recipe_source"]["directory"].startswith("/"))

    def write_report(self, root, report=PASSING_REPORT):
        (root / "native/case").mkdir(parents=True)
        (root / "twister_report.xml").write_text(report)
        (root / "native/case/handler.log").write_text("handler\n")

    def test_test_root_is_read_and_displayed_relative_to_the_repository(self):
        _, repo, _ = self.make_workspace()
        self.write_report(repo / "build/product-tests")
        build = write_build(repo)
        inputs = self.capture("--repo", repo)
        code, _, err = self.invoke("--repo", repo, "--build-dir", build, "--target", "board",
                                   "--inputs", inputs, "--test-inputs", inputs,
                                   "--test-root", repo / "build/product-tests")
        self.assertEqual(code, 0, err)
        manifest = json.loads((build / "provenance.json").read_text())
        self.assertEqual(manifest["test_summary"], {"tests": 3, "failures": 0, "errors": 0, "skipped": 0})
        self.assertEqual([artifact["path"] for artifact in manifest["test_artifacts"]],
                         ["build/product-tests/native/case/handler.log",
                          "build/product-tests/twister_report.xml"])

    def test_test_root_defaults_to_the_twister_directory_of_the_repository(self):
        _, repo, _ = self.make_workspace()
        self.write_report(repo / "build/zephyr/twister-time")
        build = write_build(repo)
        inputs = self.capture("--repo", repo)
        code, _, err = self.invoke("--repo", repo, "--build-dir", build, "--target", "board",
                                   "--inputs", inputs, "--test-inputs", inputs)
        self.assertEqual(code, 0, err)
        manifest = json.loads((build / "provenance.json").read_text())
        self.assertEqual(manifest["test_artifacts"][1]["path"], "build/zephyr/twister-time/twister_report.xml")

    def test_test_root_outside_the_repository_is_refused_clearly(self):
        _, repo, _ = self.make_workspace()
        outside = self.root / "twister"
        self.write_report(outside)
        build = write_build(repo)
        inputs = self.capture("--repo", repo)
        code, _, err = self.invoke("--repo", repo, "--build-dir", build, "--target", "board",
                                   "--inputs", inputs, "--test-inputs", inputs, "--test-root", outside)
        self.assertEqual(code, 2)
        self.assertRegex(err, "test root .* outside the repository")

    def test_incomplete_or_absent_report_under_test_root_is_still_refused(self):
        _, repo, _ = self.make_workspace()
        self.write_report(repo / "build/failing", FAILING_REPORT)
        (repo / "build/empty").mkdir()
        build = write_build(repo)
        inputs = self.capture("--repo", repo)
        for root in ("build/failing", "build/empty"):
            with self.subTest(root=root), self.assertRaisesRegex(ValueError, "complete passing"):
                self.invoke("--repo", repo, "--build-dir", build, "--target", "board", "--inputs", inputs,
                            "--test-inputs", inputs, "--test-root", repo / root)

    def test_existing_refusals_hold_for_a_product_repository(self):
        _, repo, crux = self.make_workspace()
        build = write_build(repo)
        inputs = self.capture("--repo", repo)
        arguments = ("--repo", repo, "--build-dir", build, "--target", "board", "--inputs", inputs)
        (build / "zephyr/zephyr.map").unlink()
        code, _, err = self.invoke(*arguments)
        self.assertEqual(code, 2)
        self.assertIn("build/app/zephyr/zephyr.map", err)
        (build / "zephyr/zephyr.map").write_text("map\n")

        (repo / "west.yml").write_text("manifest: {changed: true}\n")
        with self.assertRaisesRegex(ValueError, "inputs changed"):
            self.invoke(*arguments)
        git(repo, "checkout", "--", "west.yml")

        (crux / "west.yml").write_text("dirty\n")
        with self.assertRaisesRegex(ValueError, "must match its pinned revision"):
            self.invoke(*arguments)
        git(crux, "add", "west.yml")
        git(crux, "commit", "-qm", "off pin")
        with self.assertRaisesRegex(ValueError, "must match its pinned revision"):
            self.invoke(*arguments)
        self.assertFalse((build / "provenance.json").exists())

    def test_default_invocation_records_the_scripts_own_checkout(self):
        workspace, repo, _ = self.make_workspace(repository="crux-pilot", dependency="zephyr", recipe=True)
        build = write_build(repo, "build/zephyr/board")
        inputs = self.root / "inputs.json"
        with patch.object(PROVENANCE, "REPO", repo), inside_container():
            code, _, err = self.invoke("--capture-inputs", inputs)
            self.assertEqual(code, 0, err)
            code, out, err = self.invoke("--build-dir", build, "--target", "board", "--inputs", inputs)
        self.assertEqual(code, 0, err)
        self.assertEqual(self.west_directories[0], workspace)
        manifest = json.loads((build / "provenance.json").read_text())
        self.assertEqual(manifest["schema_version"], 2)
        self.assertEqual(manifest["repository"], "crux-pilot")
        self.assertEqual(manifest["build_directory"], "build/zephyr/board")
        self.assertEqual(manifest["build_id"], expected_build_id(manifest, build))
        self.assertEqual(manifest["devcontainer_recipe_source"],
                         {"directory": ".devcontainer", "origin": "repository"})
        self.assertEqual(set(manifest), {
            "schema_version", "generated_at_utc", "build_id", "repository", "target", "build_directory",
            "source", "tools", "image", "ci_run", "west_projects", "west_workspace_config_sha256",
            "test_inputs_sha256", "test_summary", "test_artifacts", "artifacts",
            "firmware_identity", "devcontainer_recipe_source"})
        labels = [line.split(":")[0] for line in out.splitlines()]
        self.assertEqual(labels, ["Build provenance", "Build identity", "Firmware identity", "Source snapshot",
                                  "Source recoverable from Git", "Test summary", "Artifacts hashed"])
        self.assertIn(f"Build identity:   {manifest['build_id']}\n", out)

    def test_default_invocation_honours_the_workspace_environment(self):
        workspace, repo, _ = self.make_workspace(repository="crux-pilot", dependency="zephyr")
        moved = self.root / "elsewhere/crux-pilot"
        moved.parent.mkdir()
        shutil.copytree(repo, moved, symlinks=True)
        os.environ["CRUX_WORKSPACE"] = str(workspace)
        with patch.object(PROVENANCE, "REPO", moved):
            self.capture()
        self.assertEqual(self.west_directories, [workspace])


if __name__ == "__main__":
    unittest.main()
