"""Check the product template: what it declares, then real west on throwaway repositories."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_crux_pins import git

try:
    import yaml  # west depends on it. The host CI job installs neither.
except ImportError:
    yaml = None

REPO = Path(__file__).resolve().parents[2]
TEMPLATE = REPO / "samples/product_template"
MANIFEST = TEMPLATE / "west.yml"
GUIDE = REPO / "docs/product-composition.md"
WEST = shutil.which("west")
GIT = shutil.which("git")
PUBLIC_BASE = "https://github.com/"
INTERNAL_BASE = "https://git.example/"
OUTSIDE = "outside-allowlist"  # a module in Zephyr's manifest that Crux did not allow


def public_targets():
    """Return the crux:: usage targets a Zephyr application can link."""
    sources = [REPO / "zephyr/CMakeLists.txt", *REPO.glob("components/*/ports/zephyr/CMakeLists.txt")]
    found = set()
    for source in sources:
        found.update(re.findall(r"add_library\((crux::\w+)\s+ALIAS", source.read_text()))
    return found


def public_header_text():
    headers = sorted(REPO.glob("components/*/include/crux/**/*.h"))
    return "\n".join(header.read_text() for header in headers)


def with_examples(text):
    """Uncomment the manifest examples. A comment line holding only # opens one."""
    lines = []
    inside = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped == "#":
            inside = True
        elif inside and stripped.startswith("# "):
            lines.append(line[:line.index("#")] + stripped[2:])
        else:
            inside = False
            lines.append(line)
    return "\n".join(lines) + "\n"


class TemplateFilesTest(unittest.TestCase):
    def setUp(self):
        self.cmake = (TEMPLATE / "app/CMakeLists.txt").read_text()
        self.conf = (TEMPLATE / "app/prj.conf").read_text()
        self.source = (TEMPLATE / "app/src/main.c").read_text()

    def test_every_file_a_team_copies_exists(self):
        for name in ("west.yml", ".gitignore", "README.md"):
            with self.subTest(name=name):
                self.assertTrue((TEMPLATE / name).is_file())

    def test_cmake_links_a_real_capability_through_public_targets_only(self):
        used = set(re.findall(r"crux::\w+", self.cmake))
        self.assertTrue(used, "the application must link a real Crux capability")
        self.assertLessEqual(used, public_targets())
        self.assertNotRegex(self.cmake, r"\bcrux_\w+", "implementation targets are not public")
        self.assertNotRegex(self.cmake, r"components/|ports/|target_include_directories")

    def test_each_linked_capability_is_enabled(self):
        for target in re.findall(r"crux::(\w+)", self.cmake):
            with self.subTest(target=target):
                self.assertIn(f"CONFIG_CRUX_{target.upper()}=y", self.conf.splitlines())

    def test_configuration_uses_real_options_and_records_the_sbom_inputs(self):
        self.assertIn("CONFIG_BUILD_OUTPUT_META=y", self.conf.splitlines())
        kconfig = (REPO / "zephyr/Kconfig").read_text()
        for option in re.findall(r"^CONFIG_(CRUX_\w+)=", self.conf, re.MULTILINE):
            with self.subTest(option=option):
                self.assertRegex(kconfig, rf"(?m)^config {option}$")

    def test_source_includes_only_public_crux_headers(self):
        self.assertNotRegex(self.source, r'#\s*include\s*"', "no private or local headers")
        headers = re.findall(r"#\s*include\s*<(crux/[^>]+)>", self.source)
        self.assertTrue(headers)
        for header in headers:
            with self.subTest(header=header):
                self.assertEqual(1, len(list(REPO.glob(f"components/*/include/{header}"))))

    def test_source_uses_only_publicly_declared_crux_names(self):
        declared = public_header_text()
        names = set(re.findall(r"\b(?:crux|CRUX)_\w+", self.source))
        self.assertTrue(names)
        for name in names:
            with self.subTest(name=name):
                self.assertRegex(declared, rf"\b{name}\b")

    def test_gitignore_keeps_the_build_directory_out_of_the_source_record(self):
        self.assertRegex((TEMPLATE / ".gitignore").read_text(), r"(?m)^/?build/$")


class TemplateScriptTest(unittest.TestCase):
    def test_the_script_builds_the_template_application_and_runs_these_tests(self):
        script = (REPO / "scripts/test-product-template.sh")
        self.assertTrue(os.access(script, os.X_OK))
        text = script.read_text()
        self.assertIn("west build -p always", text)
        self.assertIn("samples/product_template/app", text)
        self.assertIn("test_product_template.py", text)
        self.assertNotRegex(text, r"\bwest (flash|debug|attach)\b")

    def test_the_script_runs_the_pin_check_on_the_real_manifest_before_the_build(self):
        text = (REPO / "scripts/test-product-template.sh").read_text()
        self.assertIn('(cd "$workspace" && west crux-pins)', text)
        self.assertLess(text.index("west crux-pins"), text.index("west build -p always"))


class GuideTest(unittest.TestCase):
    def test_guide_says_at_the_top_that_it_is_adopted_and_untagged(self):
        top = GUIDE.read_text()[:800].lower()
        self.assertIn("adopted practice", top)
        self.assertIn("no crux release tag", top)

    def test_guide_says_at_the_top_that_the_routes_are_provisional(self):
        self.assertIn("provisional", GUIDE.read_text()[:800].lower())

    def test_examples_use_the_neutral_internal_host(self):
        for path in (GUIDE, MANIFEST):
            with self.subTest(path=path.name):
                self.assertNotIn("bitbucket.example", path.read_text())
                self.assertIn("git.example", path.read_text())

    def test_readers_can_reach_the_guide(self):
        self.assertIn("docs/product-composition.md", (REPO / "README.md").read_text())
        self.assertIn("(product-composition.md)", (REPO / "docs/build.md").read_text())
        self.assertIn("product-composition.md", (TEMPLATE / "README.md").read_text())

    def test_guide_draws_its_structure_decision_upgrade_and_release_flows(self):
        self.assertGreaterEqual(GUIDE.read_text().count("```mermaid"), 4)

    def test_guide_chooses_the_modification_route_by_kind_of_code(self):
        text = GUIDE.read_text().lower()
        for route in ("vendor branch", "different project name", "soft fork", "west blobs",
                      "outside the checkout"):
            with self.subTest(route=route):
                self.assertIn(route, text)

    def test_guide_says_a_hand_pinned_product_must_name_the_commands(self):
        self.assertIn("west-commands: scripts/west-commands.yml", GUIDE.read_text())

    def test_guide_lists_the_carried_changes_of_a_vendor_branch_as_tested(self):
        self.assertIn("git log --no-merges vendor/1.1.0..HEAD", GUIDE.read_text())

    def test_release_flow_checks_the_pins_before_it_burns_a_tag(self):
        text = GUIDE.read_text()
        tag = text.index("git -C product-x tag -a")
        self.assertLess(text.index("west crux-pins\n"), tag)
        self.assertGreater(text.rindex("west crux-pins\n"), tag)

    def test_release_bundle_lists_the_manifest_and_licences(self):
        text = GUIDE.read_text()
        self.assertIn("`release-manifest.json`", text)
        self.assertIn("`licences/`", text)

    def test_release_flow_creates_the_lock_directory_before_writing_to_it(self):
        text = GUIDE.read_text()
        self.assertLess(text.index("mkdir -p product-x/locks\n"), text.index("> product-x/locks/west.frozen.yml"))

    def test_release_build_requires_the_verified_zephyr(self):
        self.assertIn("west build -b <board> -d product-x/build/app product-x/app -- "
                      "-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON\n", GUIDE.read_text())


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.text = MANIFEST.read_text()
        self.product = yaml.safe_load(self.text)["manifest"]
        self.crux = yaml.safe_load((REPO / "west.yml").read_text())["manifest"]

    def test_manifest_imports_crux_and_names_the_product(self):
        projects = {entry["name"]: entry for entry in self.product["projects"]}
        self.assertIs(True, projects["crux-pilot"]["import"])
        self.assertEqual("product-x", self.product["self"]["path"])

    def test_crux_revision_is_main_with_a_note_to_pin_for_releases(self):
        projects = {entry["name"]: entry for entry in self.product["projects"]}
        self.assertEqual("main", projects["crux-pilot"]["revision"])
        self.assertRegex(self.text, r"(?i)pin a tag or (a )?sha")

    def test_manifest_pins_nothing_the_crux_manifest_already_pins(self):
        pinned = {entry["name"] for entry in self.crux["projects"]}
        zephyr = next(entry for entry in self.crux["projects"] if entry["name"] == "zephyr")
        pinned.update(zephyr["import"]["name-allowlist"])
        own = {entry["name"] for entry in self.product["projects"]} - {"crux-pilot"}
        self.assertEqual(set(), own & pinned)

    def test_examples_parse_and_show_group_module_and_override(self):
        examples = yaml.safe_load(with_examples(self.text))["manifest"]
        crux_groups = {group for entry in self.crux["projects"] for group in entry.get("groups", [])}
        enabled = [entry.lstrip("+") for entry in examples["group-filter"]]
        self.assertEqual(["crux-secure-management"], enabled)
        self.assertLessEqual(set(enabled), crux_groups)
        names = {entry["name"]: entry for entry in examples["projects"]}
        self.assertIn("vendor-runtime", names)
        reasons = [entry["userdata"]["crux-override"] for entry in names.values()
                   if entry.get("userdata")]
        self.assertEqual(1, len(reasons))


@unittest.skipUnless(WEST and GIT and yaml, "west, git and PyYAML are required for the workspace tests")
class TemplateWorkspaceTest(unittest.TestCase):
    """Run the template through real west. Local repositories stand in for every remote."""

    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name).resolve()
        (cls.root / "config").mkdir()
        (cls.root / "public").mkdir()
        (cls.root / "internal").mkdir()
        git_config = cls.root / "config/gitconfig"
        git_config.write_text(
            "[user]\n\tname = Template Test\n\temail = template@example.invalid\n"
            "[init]\n\tdefaultBranch = main\n"
            f'[url "{(cls.root / "public").as_uri()}/"]\n\tinsteadOf = {PUBLIC_BASE}\n'
            f'[url "{(cls.root / "internal").as_uri()}/"]\n\tinsteadOf = {INTERNAL_BASE}\n')
        # GIT_ALLOW_PROTOCOL makes a rule that fails to apply an error, never a network call.
        cls.env = {**os.environ, "GIT_CONFIG_GLOBAL": str(git_config),
                   "GIT_CONFIG_SYSTEM": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
                   "GIT_ALLOW_PROTOCOL": "file",
                   "WEST_CONFIG_GLOBAL": str(cls.root / "config/westconfig"),
                   "WEST_CONFIG_SYSTEM": str(cls.root / "config/west-system")}
        cls.env.pop("WEST_CONFIG_LOCAL", None)
        cls.env.pop("ZEPHYR_BASE", None)
        cls.build_upstream()

    @classmethod
    def commit(cls, directory, files, message):
        for name, content in files.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(directory, "add", "-A", env=cls.env)
        git(directory, "commit", "-q", "-m", message, env=cls.env)
        return git(directory, "rev-parse", "HEAD", env=cls.env)

    @classmethod
    def repository(cls, relative, files):
        directory = cls.root / relative
        directory.mkdir(parents=True)
        git(directory, "init", "-q", env=cls.env)
        return cls.commit(directory, files, "initial")

    @classmethod
    def build_upstream(cls):
        """Build fake repositories for the real Crux manifest, with the real names and groups."""
        crux_text = (REPO / "west.yml").read_text()
        crux = yaml.safe_load(crux_text)["manifest"]
        remotes = {remote["name"]: remote["url-base"] for remote in crux["remotes"]}
        zephyr_entry = next(entry for entry in crux["projects"] if entry["name"] == "zephyr")
        cls.allowlist = list(zephyr_entry["import"]["name-allowlist"])
        cls.optional = [entry for entry in crux["projects"] if entry.get("groups")]
        cls.zephyr_url = f"{PUBLIC_BASE}zephyrproject-rtos/zephyr"
        modules = ""
        cls.modules = {}
        for name in [*cls.allowlist, OUTSIDE]:
            cls.modules[name] = cls.repository(f"public/zephyrproject-rtos/{name}", {"m.txt": name})
            modules += (f"    - name: {name}\n"
                        f"      url: {PUBLIC_BASE}zephyrproject-rtos/{name}\n"
                        f"      revision: {cls.modules[name]}\n"
                        f"      path: modules/{name}\n")
        zephyr_manifest = f"manifest:\n  projects:\n{modules}  self:\n    path: zephyr\n"
        for entry in crux["projects"]:
            base = remotes[entry["remote"]].removeprefix(PUBLIC_BASE)
            repository = entry.get("repo-path", entry["name"])
            files = {"west.yml": zephyr_manifest} if entry is zephyr_entry else {"o.txt": repository}
            fake = cls.repository(f"public/{base}/{repository}", files)
            crux_text = crux_text.replace(str(entry["revision"]), fake)
            if entry is zephyr_entry:
                cls.zephyr = fake
        base = cls.remote_base().removeprefix(PUBLIC_BASE)
        directory = cls.root / f"public/{base}/crux-pilot"
        directory.mkdir(parents=True)
        git(directory, "init", "-q", env=cls.env)
        shutil.copytree(REPO / "scripts/west_commands", directory / "scripts/west_commands",
                        ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy(REPO / "scripts/west-commands.yml", directory / "scripts/west-commands.yml")
        # West writes bytecode beside the command, which the real repository ignores.
        cls.crux = cls.commit(directory, {"west.yml": crux_text, ".gitignore": (REPO / ".gitignore").read_text()},
                              "crux manifest")
        # Another Zephyr that Crux does not pin. Everything else pins by SHA, so it moves nothing.
        cls.other_zephyr = cls.commit(cls.root / "public/zephyrproject-rtos/zephyr", {"o.txt": "next"},
                                      "zephyr, one commit on")

    @staticmethod
    def remote_base():
        product = yaml.safe_load(MANIFEST.read_text())["manifest"]
        crux = next(entry for entry in product["projects"] if entry["name"] == "crux-pilot")
        return next(remote["url-base"] for remote in product["remotes"]
                    if remote["name"] == crux["remote"])

    def west(self, *arguments, cwd, check=False):
        result = subprocess.run([WEST, *arguments], cwd=cwd, env=self.env,
                                capture_output=True, text=True)
        if check and result.returncode:
            self.fail(f"west {' '.join(arguments)} failed:\n{result.stdout}\n{result.stderr}")
        return result

    def prepared(self, manifest=None):
        """Copy the template as a team would, then run west init -l."""
        workspace = Path(tempfile.mkdtemp(dir=self.root))
        product = workspace / "product-x"
        shutil.copytree(TEMPLATE, product)
        if manifest is not None:
            (product / "west.yml").write_text(manifest)
        git(product, "init", "-q", env=self.env)
        git(product, "add", "-A", env=self.env)
        git(product, "commit", "-q", "-m", "start product-x", env=self.env)
        self.west("init", "-l", str(product), cwd=workspace, check=True)
        return workspace

    def workspace(self, manifest=None):
        """Prepare a workspace, then run west update."""
        workspace = self.prepared(manifest)
        self.west("update", cwd=workspace, check=True)
        return workspace

    def active_names(self, workspace):
        return self.west("list", "-f", "{name}", cwd=workspace, check=True).stdout.split()

    def assert_pins_pass(self, workspace):
        result = self.west("crux-pins", cwd=workspace)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertNotIn("MISMATCH", result.stdout)
        return result.stdout

    def test_template_passes_init_update_and_crux_pins(self):
        workspace = self.workspace()
        output = self.assert_pins_pass(workspace)
        self.assertIn(f"OK zephyr {self.zephyr[:12]}", output)
        self.assertRegex(output, r"(?m)^CONTRACT crux-pilot [0-9a-f]{12}$")
        for name in self.allowlist:
            self.assertIn(f"OK {name} {self.modules[name][:12]}", output)
        self.assertNotIn(OUTSIDE, output)

    def test_optional_stacks_stay_off_by_default(self):
        workspace = self.workspace()
        names = self.active_names(workspace)
        for entry in self.optional:
            with self.subTest(name=entry["name"]):
                self.assertNotIn(entry["name"], names)
                self.assertFalse((workspace / entry["path"]).exists())

    def test_the_commented_examples_work_once_uncommented(self):
        text = with_examples(MANIFEST.read_text())
        manifest = yaml.safe_load(text)["manifest"]
        remotes = {remote["name"]: remote["url-base"] for remote in manifest["remotes"]}
        for entry in manifest["projects"]:
            if entry["name"] == "crux-pilot":
                continue
            relative = remotes[entry["remote"]].removeprefix(INTERNAL_BASE) + "/" + entry["name"]
            target = self.root / "internal" / relative
            if entry.get("userdata"):
                # A real fork: it holds the commit Crux pins, plus one change.
                target.parent.mkdir(parents=True, exist_ok=True)
                upstream = self.root / "public/zephyrproject-rtos" / entry["name"]
                git(self.root, "clone", "-q", str(upstream), str(target), env=self.env)
                sha = self.commit(target, {"fix.txt": "fix"}, "[acme noup] fix")
                override = entry
            else:
                sha = self.repository(f"internal/{relative}", {"v.txt": "1"})
            text = text.replace(entry["revision"], sha)
        output = self.assert_pins_pass(self.workspace(text))
        self.assertRegex(output, r"(?m)^PRODUCT vendor-runtime [0-9a-f]{12}$")
        self.assertIn(f"OVERRIDE {override['name']} ", output)
        self.assertIn(override["userdata"]["crux-override"], output)
        for entry in self.optional:
            with self.subTest(name=entry["name"]):
                self.assertIn(f"OK {entry['name']} ", output)

    def filtered(self, names):
        return MANIFEST.read_text().replace(
            "      import: true\n",
            f"      import:\n        name-allowlist: [{', '.join(names)}]\n")

    def test_a_product_filter_narrows_but_cannot_widen(self):
        kept, dropped = self.allowlist[:2]
        workspace = self.workspace(self.filtered(["zephyr", kept, OUTSIDE]))
        names = self.active_names(workspace)
        self.assertIn(kept, names)
        self.assertNotIn(dropped, names)
        self.assertNotIn(OUTSIDE, names)
        self.assert_pins_pass(workspace)

    def test_a_plain_entry_adds_a_module_crux_did_not_allow(self):
        entry = (f"    - name: {OUTSIDE}\n"
                 f"      url: {PUBLIC_BASE}zephyrproject-rtos/{OUTSIDE}\n"
                 f"      revision: {self.modules[OUTSIDE]}\n"
                 f"      path: modules/{OUTSIDE}\n")
        workspace = self.workspace(
            MANIFEST.read_text().replace("      import: true\n", "      import: true\n" + entry))
        names = self.active_names(workspace)
        self.assertIn(OUTSIDE, names)
        self.assertLessEqual(set(self.allowlist), set(names))
        self.assert_pins_pass(workspace)

    def test_redefining_zephyr_makes_the_product_own_the_allowlist(self):
        entry = (f"    - name: zephyr\n"
                 f"      url: {self.zephyr_url}\n"
                 f"      revision: {self.zephyr}\n"
                 f"      import:\n"
                 f"        name-allowlist: [{OUTSIDE}]\n")
        workspace = self.workspace(
            MANIFEST.read_text().replace("      import: true\n", "      import: true\n" + entry))
        names = self.active_names(workspace)
        self.assertIn(OUTSIDE, names)
        self.assertEqual(set(), set(self.allowlist) & set(names))
        self.assert_pins_pass(workspace)

    def test_a_release_runs_from_a_frozen_manifest_in_a_fresh_workspace(self):
        source = self.workspace()
        plain = self.west("manifest", "--freeze", cwd=source)
        self.assertNotEqual(0, plain.returncode, "an optional project is declared and not cloned")
        frozen = self.west("manifest", "--freeze", "--active-only", cwd=source, check=True).stdout
        product = source / "product-x"
        git(product, "switch", "-q", "-c", "release/1.0", env=self.env)
        self.commit(product, {"locks/west.frozen.yml": frozen}, "build: lock the manifest")
        git(product, "tag", "-a", "product-x-1.0.0", "-m", "product-x 1.0.0", env=self.env)

        fresh = Path(tempfile.mkdtemp(dir=self.root)) / "release"
        self.west("init", "-m", product.as_uri(), "--mr", "product-x-1.0.0",
                  "--mf", "locks/west.frozen.yml", str(fresh), cwd=self.root, check=True)
        self.west("update", cwd=fresh, check=True)
        self.assertIn(f"OK zephyr {self.zephyr[:12]}", self.assert_pins_pass(fresh))

        # The build record needs its build directory inside the repository, ignored.
        built = fresh / "product-x/build/app"
        built.mkdir(parents=True)
        (built / "output.txt").write_text("not a source file")
        inputs = built / "inputs.json"
        record = subprocess.run(
            [sys.executable, str(REPO / "scripts/record-build-provenance.py"),
             "--repo", str(fresh / "product-x"), "--workspace", str(fresh),
             "--capture-inputs", str(inputs)],
            cwd=fresh, env=self.env, capture_output=True, text=True)
        self.assertEqual(0, record.returncode, record.stdout + record.stderr)
        captured = json.loads(inputs.read_text())
        self.assertFalse(captured["source"]["dirty"], captured["source"]["git_status_counts"])
        commits = {entry["name"]: entry["git_commit"] for entry in captured["west_projects"]}
        self.assertEqual(self.zephyr, commits["zephyr"])

    def internal(self, entries, crux_import="      import: true\n", header=""):
        """Return the template manifest with an internal remote and more project entries."""
        return MANIFEST.read_text().replace(
            "  projects:\n",
            f"    - name: acme\n      url-base: {INTERNAL_BASE}scm/acme\n\n  projects:\n", 1
        ).replace("      import: true\n", crux_import + entries).replace(
            "  remotes:\n", header + "  remotes:\n", 1)

    @staticmethod
    def entry(name, revision, path, extra=""):
        return (f"    - name: {name}\n      remote: acme\n      revision: {revision}\n"
                f"      path: {path}\n" + extra)

    def deliver(self, repository, version, files):
        """Import a delivery as received, as the guide says, so what it dropped goes too."""
        git(repository, "switch", "-q", "vendor", env=self.env)
        git(repository, "rm", "-rq", ".", env=self.env)
        self.commit_delivery(repository, version, files)

    def commit_delivery(self, repository, version, files):
        for name, content in files.items():
            path = repository / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        git(repository, "add", "-A", "-f", env=self.env)
        git(repository, "commit", "-q", "-m", f"vendor: import delivery {version}", env=self.env)
        git(repository, "tag", "-a", f"vendor/{version}", "-m", f"delivery {version}", env=self.env)

    def test_a_vendor_branch_carries_local_changes_across_deliveries(self):
        repository = self.root / "internal/scm/acme/vendor-sdk"
        repository.mkdir(parents=True)
        git(repository, "init", "-q", env=self.env)
        git(repository, "switch", "-q", "-c", "vendor", env=self.env)
        self.commit_delivery(repository, "1.0.0", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.0.0\n",
                                                   "src/old.c": "old\n", ".gitignore": "*.a\n",
                                                   "lib/vendor.a": "binary 1.0.0\n"})
        git(repository, "switch", "-q", "-c", "main", env=self.env)
        self.commit(repository, {"src/a.c": "a 1.0.0\nlocal fix\n"}, "[acme noup] keep the local fix")
        delivery = {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.1.0\n", "src/new.c": "new\n",
                    ".gitignore": "*.a\n", "lib/vendor.a": "binary 1.1.0\n"}
        self.deliver(repository, "1.1.0", delivery)
        self.assertEqual(sorted(delivery), sorted(git(
            repository, "ls-tree", "-r", "--name-only", "vendor/1.1.0", env=self.env).splitlines()))
        git(repository, "switch", "-q", "main", env=self.env)
        git(repository, "merge", "-q", "--no-edit", "vendor/1.1.0", env=self.env)
        merge = git(repository, "rev-parse", "HEAD", env=self.env)
        self.assertEqual(2, len(git(repository, "show", "-s", "--format=%P", merge, env=self.env).split()))

        workspace = self.workspace(self.internal(self.entry("vendor-sdk", merge, "modules/vendor-sdk")))
        self.assertRegex(self.assert_pins_pass(workspace), rf"(?m)^PRODUCT vendor-sdk {merge[:12]}$")
        checkout = workspace / "modules/vendor-sdk"
        self.assertEqual("a 1.0.0\nlocal fix\n", (checkout / "src/a.c").read_text())
        self.assertEqual("b 1.1.0\n", (checkout / "src/b.c").read_text())
        self.assertEqual("new\n", (checkout / "src/new.c").read_text())
        self.assertEqual("binary 1.1.0\n", (checkout / "lib/vendor.a").read_text())
        self.assertFalse((checkout / "src/old.c").exists())
        carried = git(checkout, "log", "--no-merges", "--format=%s", "vendor/1.1.0..HEAD", env=self.env)
        self.assertEqual("[acme noup] keep the local fix", carried)
        self.assertEqual("src/a.c", git(checkout, "diff", "--name-only", "vendor/1.1.0", "HEAD", env=self.env))

    def commercial_edition(self, name):
        return self.repository(f"internal/scm/acme/{name}", {"edition.txt": "commercial"})

    def test_a_commercial_edition_under_a_new_name_with_the_group_off_is_a_product_module(self):
        revision = self.commercial_edition("wolfssl-commercial")
        workspace = self.workspace(self.internal(
            self.entry("wolfssl-commercial", revision, "modules/crypto/wolfssl-commercial")))
        output = self.assert_pins_pass(workspace)
        self.assertRegex(output, rf"(?m)^PRODUCT wolfssl-commercial {revision[:12]}$")
        self.assertNotRegex(output, r"(?m)^\w+ wolfssl ")
        self.assertFalse((workspace / "modules/crypto/wolfssl").exists())

    def test_a_commercial_edition_can_take_the_path_only_when_the_original_is_filtered_out(self):
        revision = self.commercial_edition("wolfssh-commercial")
        entry = self.entry("wolfssh-commercial", revision, "modules/crypto/wolfssh")
        taken = self.west("update", cwd=self.prepared(self.internal(entry)))
        self.assertNotEqual(0, taken.returncode, "an inactive project still holds its path")
        self.assertIn('path "modules/crypto/wolfssh" is taken', taken.stderr)

        names = ["zephyr", *self.allowlist, *(e["name"] for e in self.optional if e["name"] != "wolfssh")]
        allow = f"      import:\n        name-allowlist: [{', '.join(names)}]\n"
        workspace = self.workspace(self.internal(
            entry, allow, header="  group-filter: [+crux-secure-management]\n"))
        output = self.assert_pins_pass(workspace)
        self.assertRegex(output, rf"(?m)^PRODUCT wolfssh-commercial {revision[:12]}$")
        self.assertRegex(output, r"(?m)^OK wolfssl ")
        self.assertNotRegex(output, r"(?m)^\w+ wolfssh ")

    def test_reusing_a_crux_name_without_the_contract_commit_fails(self):
        # test_crux_pins.py covers this for zephyr, which fails earlier. This is any other project.
        revision = self.commercial_edition("wolfpsa")
        reason = '      userdata:\n        crux-override: "commercial edition"\n'
        for extra in ("", reason):
            with self.subTest(extra=extra):
                workspace = self.workspace(self.internal(
                    self.entry("wolfpsa", revision, "modules/crypto/wolfpsa", extra)))
                result = self.west("crux-pins", cwd=workspace)
                self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertNotRegex(result.stdout, r"(?m)^OK ")
                self.assertIn("wolfpsa: cannot resolve the contract revision", result.stderr)

    def test_a_zephyr_soft_fork_is_an_override_and_restates_the_allowlist(self):
        fork = self.root / "internal/scm/acme/zephyr"
        fork.parent.mkdir(parents=True, exist_ok=True)
        git(self.root, "clone", "-q", str(self.root / "public/zephyrproject-rtos/zephyr"), str(fork), env=self.env)
        git(fork, "reset", "-q", "--hard", self.zephyr, env=self.env)
        revision = self.commit(fork, {"fix.txt": "fix"}, "[acme noup] fix")
        git(fork, "tag", "v4.4.2-acme3", env=self.env)
        entry = (f"    - name: zephyr\n      remote: acme\n      revision: {revision}\n"
                 f"      import:\n        name-allowlist: [{self.allowlist[0]}]\n"
                 '      userdata:\n        crux-override: "acme fix"\n')
        workspace = self.workspace(self.internal(entry))
        output = self.assert_pins_pass(workspace)
        self.assertIn(f"OVERRIDE zephyr {revision[:12]} (contract {self.zephyr[:12]}): acme fix", output)
        self.assertIn(f"OK {self.allowlist[0]} ", output)
        self.assertNotIn(self.allowlist[1], output, "the import through Crux is gone")

    def hand_pinned(self, zephyr=None, commands=True, name="zephyr", modules=None):
        """Return a product manifest that imports nothing and pins each project itself."""
        modules = [self.allowlist[0], OUTSIDE] if modules is None else modules
        pinned = "".join(f"    - name: {module}\n      remote: zephyrproject-rtos\n"
                         f"      revision: {self.modules[module]}\n      path: modules/{module}\n"
                         for module in modules)
        west_commands = "      west-commands: scripts/west-commands.yml\n" if commands else ""
        return ("manifest:\n"
                '  version: "1.2"\n'
                "  remotes:\n"
                f"    - name: crux\n      url-base: {self.remote_base()}\n"
                f"    - name: zephyrproject-rtos\n      url-base: {PUBLIC_BASE}zephyrproject-rtos\n"
                "  projects:\n"
                f"    - name: crux-pilot\n      remote: crux\n      revision: {self.crux}\n"
                f"{west_commands}"
                f"    - name: {name}\n      remote: zephyrproject-rtos\n      repo-path: zephyr\n"
                f"      revision: {zephyr or self.zephyr}\n"
                f"{pinned}"
                "  self:\n    path: product-x\n")

    def test_a_hand_pinned_product_passes_while_its_pins_match(self):
        output = self.assert_pins_pass(self.workspace(self.hand_pinned()))
        self.assertRegex(output, rf"(?m)^CONTRACT crux-pilot {self.crux[:12]}$")
        self.assertIn(f"OK zephyr {self.zephyr[:12]}", output)
        self.assertIn(f"OK {self.allowlist[0]} {self.modules[self.allowlist[0]][:12]}", output)
        # Outside Crux's allowlist, but in Zephyr's manifest at Crux's pin.
        self.assertIn(f"OK {OUTSIDE} {self.modules[OUTSIDE][:12]}", output)
        # Only what the workspace holds is reported. A module left out says nothing.
        self.assertNotIn(self.allowlist[1], output)

    def test_a_hand_pinned_product_on_another_zephyr_is_a_mismatch(self):
        result = self.west("crux-pins", cwd=self.workspace(self.hand_pinned(self.other_zephyr)))
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn(f"MISMATCH zephyr {self.other_zephyr[:12]} (contract {self.zephyr[:12]})", result.stdout)

    def test_a_hand_pinned_product_gets_the_command_only_by_naming_it(self):
        result = self.west("crux-pins", cwd=self.workspace(self.hand_pinned(commands=False)))
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("crux-pins", result.stderr)
        self.assertNotIn("CONTRACT", result.stdout)

    def test_a_hand_pinned_zephyr_must_keep_its_name(self):
        result = self.west("crux-pins", cwd=self.workspace(self.hand_pinned(name="rtos")))
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("no zephyr project is pinned", result.stderr)


if __name__ == "__main__":
    unittest.main()
