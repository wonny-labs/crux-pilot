"""Check the Crux pin comparison: pure logic first, then real west on throwaway repositories."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
COMMAND = REPO / "scripts/west_commands/crux_pins.py"
COMMAND_LIST = REPO / "scripts/west-commands.yml"
WEST = shutil.which("west")
GIT = shutil.which("git")


def load_module():
    # Loaded by path and without west on purpose: the logic must not need it.
    spec = importlib.util.spec_from_file_location("crux_pins", COMMAND)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def project(name, revision, active=True, userdata=None):
    return {"name": name, "revision": revision, "active": active, "userdata": userdata}


class FakeResolver:
    """Resolve names to commits from a table, as git would inside a checkout."""

    def __init__(self, table):
        self.table = table

    def __call__(self, name, revision, role):
        try:
            return self.table[(name, revision)]
        except KeyError:
            raise ValueError(f"{name}: cannot resolve {revision}")


SHA_A = "a" * 40
SHA_B = "b" * 40
SHA_C = "c" * 40


class ComparisonTest(unittest.TestCase):
    def setUp(self):
        self.pins = load_module()

    def compare(self, contract, projects, table, skip=("crux-pilot",)):
        return self.pins.compare_pins(contract, projects, FakeResolver(table), skip=skip)

    def test_equal_revision_is_ok(self):
        results = self.compare({"hal": SHA_A}, [project("hal", SHA_A)],
                               {("hal", SHA_A): SHA_A})
        self.assertEqual([("OK", "hal")], [(r["status"], r["name"]) for r in results])
        self.assertIn("OK hal " + SHA_A[:12], self.pins.format_result(results[0]))

    def test_tag_and_sha_that_resolve_alike_are_equal(self):
        table = {("hal", "v1.0"): SHA_A, ("hal", SHA_A): SHA_A}
        results = self.compare({"hal": "v1.0"}, [project("hal", SHA_A)], table)
        self.assertEqual("OK", results[0]["status"])

    def test_same_string_resolving_apart_is_not_trusted_by_string(self):
        # The contract name may point to another commit in a fork than in upstream.
        table = {("hal", "main"): SHA_B}
        results = self.compare({"hal": SHA_A}, [project("hal", "main")],
                               {**table, ("hal", SHA_A): SHA_A})
        self.assertEqual("MISMATCH", results[0]["status"])

    def test_override_needs_a_reason(self):
        table = {("hal", SHA_A): SHA_A, ("hal", SHA_B): SHA_B}
        declared = project("hal", SHA_B, userdata={"crux-override": "vendor fix 123"})
        results = self.compare({"hal": SHA_A}, [declared], table)
        self.assertEqual("OVERRIDE", results[0]["status"])
        line = self.pins.format_result(results[0])
        self.assertIn(f"OVERRIDE hal {SHA_B[:12]} (contract {SHA_A[:12]}): vendor fix 123", line)

    def test_difference_without_reason_is_a_mismatch(self):
        table = {("hal", SHA_A): SHA_A, ("hal", SHA_B): SHA_B}
        for userdata in (None, {}, {"other": "x"}, {"crux-override": ""}, {"crux-override": "  "}):
            with self.subTest(userdata=userdata):
                results = self.compare({"hal": SHA_A}, [project("hal", SHA_B, userdata=userdata)], table)
                self.assertEqual("MISMATCH", results[0]["status"])
                self.assertIn(f"MISMATCH hal {SHA_B[:12]} (contract {SHA_A[:12]})",
                              self.pins.format_result(results[0]))

    def test_override_on_a_matching_project_is_just_ok(self):
        results = self.compare({"hal": SHA_A},
                               [project("hal", SHA_A, userdata={"crux-override": "stale"})],
                               {("hal", SHA_A): SHA_A})
        self.assertEqual("OK", results[0]["status"])

    def test_project_outside_the_contract_is_product(self):
        results = self.compare({}, [project("driver", SHA_C)], {("driver", SHA_C): SHA_C})
        self.assertEqual("PRODUCT", results[0]["status"])
        self.assertEqual(f"PRODUCT driver {SHA_C[:12]}", self.pins.format_result(results[0]))

    def test_inactive_and_skipped_projects_are_not_reported(self):
        projects = [project("optional", SHA_B, active=False), project("crux-pilot", SHA_C),
                    project("manifest", SHA_C)]
        results = self.compare({"optional": SHA_A}, projects, {}, skip=("crux-pilot", "manifest"))
        self.assertEqual([], results)

    def test_unreadable_contract_never_reports_ok(self):
        # The contract revision is not in the checkout, as in a fork without that commit.
        with self.assertRaises(self.pins.ContractError) as caught:
            self.compare({"hal": SHA_A}, [project("hal", SHA_A)], {})
        self.assertIn("hal", str(caught.exception))

    def test_every_unresolvable_project_is_named(self):
        with self.assertRaises(self.pins.ContractError) as caught:
            self.compare({"one": SHA_A, "two": SHA_A},
                         [project("one", SHA_A), project("two", SHA_A)], {})
        self.assertIn("one", str(caught.exception))
        self.assertIn("two", str(caught.exception))

    def test_explicit_crux_entries_win_over_zephyr(self):
        merged = self.pins.merge_contract(
            [{"name": "hal", "revision": "crux-rev"}, {"name": "extra"}],
            [{"name": "hal", "revision": "zephyr-rev"}, {"name": "cmsis", "revision": "z"}])
        self.assertEqual({"hal": "crux-rev", "cmsis": "z"}, merged)

    def test_count_of_mismatches(self):
        table = {("a", SHA_A): SHA_A, ("a", SHA_B): SHA_B, ("b", SHA_A): SHA_A, ("b", SHA_B): SHA_B}
        results = self.compare({"a": SHA_A, "b": SHA_A},
                               [project("a", SHA_B), project("b", SHA_B)], table)
        self.assertEqual(2, self.pins.count_mismatches(results))


def git(directory, *arguments, env=None):
    return subprocess.run(["git", "-C", str(directory), *arguments], check=True,
                          capture_output=True, text=True, env=env).stdout.strip()


@unittest.skipUnless(WEST and GIT, "west and git are required for the workspace tests")
class WorkspaceTest(unittest.TestCase):
    """Run the real command against local repositories. No network, no real Zephyr."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        (self.root / "config").mkdir()
        self.git_config = self.root / "config/gitconfig"
        self.git_config.write_text("[user]\n\tname = Pins Test\n\temail = pins@example.invalid\n"
                                   "[init]\n\tdefaultBranch = main\n"
                                   "[protocol \"file\"]\n\tallow = always\n")
        self.env = {**os.environ, "GIT_CONFIG_GLOBAL": str(self.git_config),
                    "GIT_CONFIG_SYSTEM": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
                    "WEST_CONFIG_GLOBAL": str(self.root / "config/westconfig"),
                    "WEST_CONFIG_SYSTEM": str(self.root / "config/west-system")}
        self.env.pop("WEST_CONFIG_LOCAL", None)
        self.repos = self.root / "repos"
        self.repos.mkdir()
        self.build_upstream()

    def commit(self, directory, files, message, tag=None):
        for name, content in files.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(directory, "add", "-A", env=self.env)
        git(directory, "commit", "-q", "-m", message, env=self.env)
        if tag:
            git(directory, "tag", tag, env=self.env)
        return git(directory, "rev-parse", "HEAD", env=self.env)

    def init(self, name):
        directory = self.repos / name
        directory.mkdir()
        git(directory, "init", "-q", env=self.env)
        return directory

    def url(self, name):
        return (self.repos / name).as_uri()

    def build_upstream(self):
        module = self.init("fake-module")
        self.module_v1 = self.commit(module, {"m.txt": "1"}, "module one", tag="v1")
        self.module_v2 = self.commit(module, {"m.txt": "2"}, "module two", tag="v2")
        git(module, "branch", "stable", self.module_v1, env=self.env)
        optional = self.init("optional-module")
        self.optional = self.commit(optional, {"o.txt": "1"}, "optional one")
        zephyr = self.init("zephyr")
        self.zephyr_one = self.commit(zephyr, {"west.yml": self.zephyr_manifest("v1")}, "zephyr one")
        self.zephyr_two = self.commit(zephyr, {"west.yml": self.zephyr_manifest(self.module_v2)},
                                      "zephyr two")
        crux = self.init("crux-pilot")
        shutil.copytree(REPO / "scripts/west_commands", crux / "scripts/west_commands")
        shutil.copy(COMMAND_LIST, crux / "scripts/west-commands.yml")
        manifest = (
            "manifest:\n"
            "  group-filter: [-crux-optional]\n"
            "  projects:\n"
            "    - name: zephyr\n"
            f"      url: {self.url('zephyr')}\n"
            f"      revision: {self.zephyr_one}\n"
            "      import:\n"
            "        name-allowlist: [fake-module]\n"
            "    - name: optional-module\n"
            f"      url: {self.url('optional-module')}\n"
            f"      revision: {self.optional}\n"
            "      path: modules/optional\n"
            "      groups: [crux-optional]\n"
            "  self:\n"
            "    path: crux-pilot\n"
            "    west-commands: scripts/west-commands.yml\n")
        self.crux = self.commit(crux, {"west.yml": manifest}, "crux manifest")

    def zephyr_manifest(self, module_revision):
        return ("manifest:\n"
                "  projects:\n"
                "    - name: fake-module\n"
                f"      url: {self.url('fake-module')}\n"
                f"      revision: {module_revision}\n"
                "      path: modules/fake\n"
                "  self:\n"
                "    path: zephyr\n")

    def product(self, extra_projects="", header=""):
        manifest = ("manifest:\n" + header +
                    "  projects:\n"
                    "    - name: crux-pilot\n"
                    f"      url: {self.url('crux-pilot')}\n"
                    f"      revision: {self.crux}\n"
                    "      import: true\n" + extra_projects +
                    "  self:\n"
                    "    path: product-x\n")
        workspace = self.root / "workspace"
        shutil.rmtree(workspace, ignore_errors=True)
        directory = workspace / "product-x"
        directory.mkdir(parents=True)
        git(directory, "init", "-q", env=self.env)
        (directory / "west.yml").write_text(manifest)
        git(directory, "add", "west.yml", env=self.env)
        git(directory, "commit", "-q", "-m", "product manifest", env=self.env)
        self.west("init", "-l", str(directory), cwd=workspace, check=True)
        self.west("update", cwd=workspace, check=True)
        return workspace

    def west(self, *arguments, cwd, check=False):
        result = subprocess.run(["west", *arguments], cwd=cwd, env=self.env,
                                capture_output=True, text=True)
        if check and result.returncode:
            self.fail(f"west {' '.join(arguments)} failed:\n{result.stdout}\n{result.stderr}")
        return result

    def pins(self, workspace):
        return self.west("crux-pins", cwd=workspace)

    def test_clean_import_passes(self):
        result = self.pins(self.product())
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OK fake-module " + self.module_v1[:12], result.stdout)
        self.assertNotIn("optional-module", result.stdout)

    def test_crux_as_the_manifest_repository_uses_its_working_tree(self):
        workspace = self.root / "crux-workspace"
        workspace.mkdir()
        git(workspace, "clone", "-q", self.url("crux-pilot"), "crux-pilot", env=self.env)
        self.west("init", "-l", str(workspace / "crux-pilot"), cwd=workspace, check=True)
        self.west("update", cwd=workspace, check=True)
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OK fake-module " + self.module_v1[:12], result.stdout)

    def test_a_tag_and_its_commit_are_the_same_pin(self):
        workspace = self.product(
            "    - name: fake-module\n"
            f"      url: {self.url('fake-module')}\n"
            f"      revision: {self.module_v1}\n"
            "      path: modules/fake\n")
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OK fake-module", result.stdout)

    def test_a_branch_name_is_resolved_to_its_commit(self):
        workspace = self.product(
            "    - name: fake-module\n"
            f"      url: {self.url('fake-module')}\n"
            "      revision: stable\n"
            "      path: modules/fake\n")
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OK fake-module " + self.module_v1[:12], result.stdout)

    def test_undeclared_module_revision_fails(self):
        workspace = self.product(
            "    - name: fake-module\n"
            f"      url: {self.url('fake-module')}\n"
            f"      revision: {self.module_v2}\n"
            "      path: modules/fake\n")
        result = self.pins(workspace)
        self.assertNotEqual(0, result.returncode)
        self.assertIn(f"MISMATCH fake-module {self.module_v2[:12]} (contract {self.module_v1[:12]})",
                      result.stdout)
        self.assertIn("1 ", result.stderr)

    def test_declared_override_passes(self):
        workspace = self.product(
            "    - name: fake-module\n"
            f"      url: {self.url('fake-module')}\n"
            f"      revision: {self.module_v2}\n"
            "      path: modules/fake\n"
            "      userdata:\n"
            "        crux-override: \"vendor fix 123\"\n")
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OVERRIDE fake-module", result.stdout)
        self.assertIn("vendor fix 123", result.stdout)

    def test_another_zephyr_is_reported_even_though_the_module_follows_it(self):
        workspace = self.product(
            "    - name: zephyr\n"
            f"      url: {self.url('zephyr')}\n"
            f"      revision: {self.zephyr_two}\n"
            "      import:\n"
            "        name-allowlist: [fake-module]\n")
        result = self.pins(workspace)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("MISMATCH zephyr " + self.zephyr_two[:12], result.stdout)
        self.assertIn("MISMATCH fake-module " + self.module_v2[:12], result.stdout)

    def test_enabling_an_optional_group_without_a_revision_passes(self):
        workspace = self.product(header="  group-filter: [+crux-optional]\n")
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OK optional-module " + self.optional[:12], result.stdout)

    def test_product_only_project_is_listed_and_passes(self):
        extra = self.init("driver")
        revision = self.commit(extra, {"d.txt": "1"}, "driver")
        workspace = self.product(
            "    - name: driver\n"
            f"      url: {self.url('driver')}\n"
            f"      revision: {revision}\n")
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("PRODUCT driver " + revision[:12], result.stdout)

    def test_a_frozen_manifest_keeps_the_override(self):
        workspace = self.product(
            "    - name: fake-module\n"
            f"      url: {self.url('fake-module')}\n"
            f"      revision: {self.module_v2}\n"
            "      path: modules/fake\n"
            "      userdata:\n"
            "        crux-override: \"vendor fix 123\"\n")
        frozen = self.west("manifest", "--freeze", "--active-only", cwd=workspace, check=True).stdout
        self.assertIn("crux-override", frozen)
        (workspace / "product-x/west.yml").write_text(frozen)
        result = self.pins(workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("OVERRIDE fake-module", result.stdout)

    def test_a_fork_without_the_contract_commit_is_an_error_not_ok(self):
        fork = self.init("zephyr-fork")
        revision = self.commit(fork, {"west.yml": self.zephyr_manifest("v1")}, "unrelated history")
        workspace = self.product(
            "    - name: zephyr\n"
            f"      url: {self.url('zephyr-fork')}\n"
            f"      revision: {revision}\n"
            "      import:\n"
            "        name-allowlist: [fake-module]\n")
        result = self.pins(workspace)
        output = result.stdout + result.stderr
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn("OK ", result.stdout)
        self.assertIn("contract", output.lower())
        self.assertIn(self.zephyr_one, output)


if __name__ == "__main__":
    unittest.main()
