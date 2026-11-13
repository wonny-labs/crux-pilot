"""Check the product template: what it declares, then real west on throwaway repositories."""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from test_crux_pins import git

try:
    import yaml  # west depends on it. The host CI job installs neither.
except ImportError:
    yaml = None

REPO = Path(__file__).resolve().parents[2]
TEMPLATE = REPO / "templates/product"
MANIFEST = TEMPLATE / "west.yml"
GUIDE = REPO / "docs/product-composition.md"
RELEASE = TEMPLATE / "RELEASE.md"
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


def guide_section(heading):
    """Return one section of the guide, from its heading to the next at that level or above."""
    level = len(heading.split(" ")[0])
    kept = None
    fenced = False
    for line in GUIDE.read_text().splitlines():
        if line.startswith("```"):
            fenced = not fenced
        hashes = re.match(r"#+ ", line) if not fenced else None
        if kept is None:
            kept = [line] if line == heading else None
        elif hashes and len(hashes.group().strip()) <= level:
            break
        else:
            kept.append(line)
    return "\n".join(kept)


def guide_commands(marker):
    """Return the fenced shell block of the guide that holds a marker, so a test runs what it says."""
    blocks = re.findall(r"```sh\n(.*?)```", GUIDE.read_text(), re.DOTALL)
    return next(block for block in blocks if marker in block)


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
        for name in ("west.yml", ".gitignore", "README.md", "RELEASE.md"):
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
        # The shell may name a header of the product's own domain, and no other quoted header.
        for header in re.findall(r'#\s*include\s*"([^"]+)"', self.source):
            with self.subTest(header=header):
                resolved = (TEMPLATE / "app/src" / header).resolve()
                self.assertTrue(resolved.is_file())
                self.assertTrue(resolved.is_relative_to((TEMPLATE / "domain").resolve()))
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


class TemplateLocationTest(unittest.TestCase):
    """The template moved from samples/product_template to templates/product on 9 November 2026."""

    def test_the_template_files_are_under_templates_product(self):
        for name in (".gitignore", "README.md", "RELEASE.md", "west.yml",
                     "app/CMakeLists.txt", "app/prj.conf", "app/src/main.c",
                     "domain/product_domain.h", "domain/product_domain.c",
                     "domain/product_port.h", "check_domain_includes.py"):
            with self.subTest(name=name):
                self.assertTrue((REPO / "templates/product" / name).is_file())

    def test_the_old_location_is_gone_and_leaves_no_stub(self):
        self.assertFalse((REPO / "samples/product_template").exists())

    def test_the_readme_records_the_move(self):
        head = "\n".join((TEMPLATE / "README.md").read_text().splitlines()[:8])
        self.assertIn("samples/product_template", head)
        self.assertIn("9 November 2026", head)
        self.assertNotIn("samples/product_template", RELEASE.read_text())

    def test_the_copy_command_names_the_new_location(self):
        text = (TEMPLATE / "README.md").read_text()
        self.assertIn("cp -R crux-pilot/templates/product acme-workspace/product-x", text)
        self.assertNotIn("cp -R crux-pilot/samples/", text)


class TemplateScriptTest(unittest.TestCase):
    def test_the_script_builds_the_template_application_and_runs_these_tests(self):
        script = (REPO / "scripts/test-product-template.sh")
        self.assertTrue(os.access(script, os.X_OK))
        text = script.read_text()
        self.assertIn("west build -p always", text)
        self.assertIn("templates/product/app", text)
        self.assertIn("test_product_template.py", text)
        self.assertNotRegex(text, r"\bwest (flash|debug|attach)\b")

    def test_the_script_runs_the_pin_check_on_the_real_manifest_before_the_build(self):
        text = (REPO / "scripts/test-product-template.sh").read_text()
        self.assertIn('(cd "$workspace" && west crux-pins)', text)
        self.assertLess(text.index("west crux-pins"), text.index("west build -p always"))


FROZEN_CRUX_HEADERS = ("crux/thread.h", "crux/sync.h", "crux/queue.h", "crux/serial.h")
ZEPHYR_HEADERS = ("zephyr/kernel.h", "zephyr/sys/printk.h", "zephyr/device.h", "zephyr/net/socket.h")


def load_domain_check():
    spec = importlib.util.spec_from_file_location(
        "check_domain_includes", TEMPLATE / "check_domain_includes.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TemplateDomainSplitTest(unittest.TestCase):
    """The template has a plain C domain and a shell that calls it through one product-owned port."""

    def setUp(self):
        self.domain = TEMPLATE / "domain"
        self.shell = (TEMPLATE / "app/src/main.c").read_text()
        self.cmake = (TEMPLATE / "app/CMakeLists.txt").read_text()

    def test_the_domain_has_one_port_header_and_no_zephyr_or_frozen_include(self):
        self.assertEqual(["product_domain.c", "product_domain.h", "product_port.h"],
                         sorted(path.name for path in self.domain.iterdir()))
        for path in self.domain.iterdir():
            with self.subTest(path=path.name):
                text = path.read_text()
                self.assertNotRegex(text, r"zephyr|\bk_[a-z]+_\w+|CONFIG_", "plain C only")
                self.assertNotRegex(text, r"crux/(thread|sync|queue|serial)\.h")

    def test_the_domain_declares_what_it_needs_in_the_port_and_the_shell_provides_it(self):
        port = (self.domain / "product_port.h").read_text()
        names = re.findall(r"\b(product_port_\w+)\s*\(", port)
        self.assertTrue(names)
        for name in names:
            with self.subTest(name=name):
                self.assertRegex(self.shell, rf"(?m)^\w[\w \*]*\b{name}\(")
                self.assertRegex((self.domain / "product_domain.c").read_text(), rf"\b{name}\(")
        domain_header = (self.domain / "product_domain.h").read_text()
        self.assertNotRegex(domain_header, r"product_port_\w+\(", "the header does not define the port")

    def test_the_build_compiles_the_domain_sources_without_adding_include_paths(self):
        self.assertIn("../domain/product_domain.c", self.cmake)
        self.assertNotRegex(self.cmake, r"target_include_directories|zephyr_include_directories")

    def test_the_shell_calls_the_domain_and_takes_no_frozen_header(self):
        self.assertIn("product_domain_step(", self.shell)
        for header in FROZEN_CRUX_HEADERS:
            with self.subTest(header=header):
                self.assertNotIn(f"<{header}>", self.shell)

    @unittest.skipUnless(shutil.which("cc"), "needs a host C compiler")
    def test_the_domain_builds_and_behaves_on_the_host_with_a_fake_port(self):
        fake = r'''
#include <stdio.h>
#include "product_domain.h"
#include "product_port.h"
static const int32_t script[] = {300000, 300000, 340000, 340000, 340000, 340000, 340000, 340000};
static unsigned next;
int product_port_read_temperature(int32_t *millikelvin)
{
    if (next >= sizeof script / sizeof script[0]) {
        return -1;
    }
    *millikelvin = script[next++];
    return 0;
}
int main(void)
{
    struct product_domain d;
    product_domain_init(&d, 320000);
    for (int i = 0; i < 8; i++) {
        int rc = product_domain_step(&d);
        printf("%d %d %d\n", rc, (int)product_domain_filtered(&d), product_domain_alarm(&d) ? 1 : 0);
    }
    product_domain_set_limit(&d, 500000);
    next = 0;
    product_domain_step(&d);
    printf("limit %d\n", product_domain_alarm(&d) ? 1 : 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "fake.c"
            source.write_text(fake)
            binary = Path(tmp) / "fake"
            subprocess.run(["cc", "-std=c17", "-Wall", "-Wextra", "-Werror", "-I", str(self.domain),
                            str(source), str(self.domain / "product_domain.c"), "-o", str(binary)],
                           check=True)
            lines = subprocess.run([str(binary)], check=True, capture_output=True,
                                   text=True).stdout.splitlines()
        steps = [line.split() for line in lines[:8]]
        self.assertEqual(["0", "300000", "0"], steps[0])
        self.assertEqual("0", steps[1][0])
        self.assertEqual("0", steps[2][2], "one hot sample is filtered below the limit")
        self.assertEqual("1", steps[-1][2], "a sustained hot input raises the alarm")
        self.assertEqual("limit 0", lines[8], "a new limit applies to the state at once")


class DomainIncludeCheckTest(unittest.TestCase):
    """The check rejects the four frozen Crux headers and every Zephyr header in the domain."""

    @classmethod
    def setUpClass(cls):
        cls.check = load_domain_check()

    def domain_copy(self, tmp):
        root = Path(tmp) / "domain"
        shutil.copytree(TEMPLATE / "domain", root)
        return root

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(TEMPLATE / "check_domain_includes.py"), *args],
                              capture_output=True, text=True)

    def violations_of(self, text, name="product_domain.c", prepend=True):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.domain_copy(tmp)
            path = root / name
            path.write_text(text + path.read_text() if prepend else path.read_text() + text)
            return self.check.check(root)

    def test_the_template_as_shipped_passes(self):
        self.assertEqual([], self.check.check(TEMPLATE / "domain"))

    def test_the_command_passes_on_the_template_as_shipped(self):
        result = self.run_cli()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("passed", result.stdout)

    def test_each_frozen_crux_header_fails(self):
        for header in FROZEN_CRUX_HEADERS:
            with self.subTest(header=header):
                found = self.violations_of(f"#include <{header}>\n")
                self.assertEqual([header], [v.include for v in found])
                self.assertIn("frozen", found[0].reason)

    def test_each_zephyr_header_fails(self):
        for header in ZEPHYR_HEADERS:
            with self.subTest(header=header):
                found = self.violations_of(f"#include <{header}>\n")
                self.assertEqual([header], [v.include for v in found])
                self.assertIn("Zephyr", found[0].reason)

    def test_the_quoted_form_fails_too(self):
        for header in (FROZEN_CRUX_HEADERS[0], ZEPHYR_HEADERS[0]):
            with self.subTest(header=header):
                found = self.violations_of(f'#include "{header}"\n')
                self.assertEqual([header], [v.include for v in found])

    def test_a_header_file_and_a_file_of_any_suffix_are_checked(self):
        found = self.violations_of("#include <crux/queue.h>\n", "product_domain.h")
        self.assertEqual(["product_domain.h"], [Path(v.path).name for v in found])
        with tempfile.TemporaryDirectory() as tmp:
            root = self.domain_copy(tmp)
            (root / "helper.inc").write_text("#include <zephyr/kernel.h>\n")
            self.assertEqual(["helper.inc"], [Path(v.path).name for v in self.check.check(root)])

    def test_directive_spellings_that_a_compiler_accepts_fail(self):
        spellings = (
            "  #  include <crux/thread.h>\n",
            "%:include <crux/thread.h>\n",
            "??=include <crux/thread.h>\n",
            "#include_next <crux/thread.h>\n",
            "#import <crux/thread.h>\n",
            "#include <crux/\\\nthread.h>\n",
            "#include <crux/thread.h> // trailing comment\n",
            "/* note */ #include <crux/thread.h>\n",
            "#include <crux/thread.h>\r\n",
        )
        for text in spellings:
            with self.subTest(text=text):
                found = self.violations_of(text)
                self.assertEqual(["crux/thread.h"], [v.include for v in found])

    def test_a_header_named_only_in_a_comment_or_string_passes(self):
        text = '/* never #include <crux/thread.h> or <zephyr/kernel.h> here */\n// <crux/sync.h>\n'
        self.assertEqual([], self.violations_of(text))
        self.assertEqual([], self.violations_of('static const char *note = "#include <crux/queue.h>";\n'))

    def test_the_other_public_crux_headers_and_iso_c_stay_allowed(self):
        text = "#include <crux/time.h>\n#include <crux/status.h>\n#include <stdint.h>\n#include <stdbool.h>\n"
        self.assertEqual([], self.violations_of(text))

    def test_an_include_that_cannot_be_read_or_leaves_the_tree_fails(self):
        for text in ("#define H <crux/thread.h>\n#include H\n",
                     '#include "../app/src/main.c"\n',
                     '#include "missing.h"\n'):
            with self.subTest(text=text):
                self.assertTrue(self.violations_of(text), text)

    def test_the_command_names_the_file_line_and_header_and_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.domain_copy(tmp)
            path = root / "product_domain.c"
            path.write_text(path.read_text() + "#include <zephyr/kernel.h>\n")
            line = len(path.read_text().splitlines())
            result = self.run_cli(str(root))
        self.assertEqual(1, result.returncode)
        self.assertIn(f"product_domain.c:{line}: zephyr/kernel.h", result.stdout)

    def test_a_missing_domain_directory_is_an_error_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.run_cli(str(Path(tmp) / "nowhere"))
        self.assertEqual(2, result.returncode)
        self.assertNotIn("passed", result.stdout)


class SharedStatePatternTest(unittest.TestCase):
    """The shell shows the pattern of docs/choosing-an-api.md: one owner, snapshot reads, message writes."""

    def setUp(self):
        self.shell = (TEMPLATE / "app/src/main.c").read_text()

    def function(self, name):
        match = re.search(rf"(?ms)^[\w \*]*\b{name}\([^)]*\)\s*\{{(.*?)^\}}", self.shell)
        self.assertIsNotNone(match, f"the shell defines {name}")
        return match.group(1)

    def test_one_owner_thread_runs_the_domain_and_nothing_else_does(self):
        self.assertIn("K_THREAD_DEFINE(", self.shell)
        pattern = r"\bproduct_domain_(init|step|set_limit)\("
        owner = self.function("product_owner_entry")
        self.assertTrue(re.search(pattern, owner))
        self.assertEqual(len(re.findall(pattern, self.shell)), len(re.findall(pattern, owner)),
                         "only the owner thread calls the domain")

    def test_a_status_read_is_a_snapshot_read_with_an_atomic_and_takes_no_lock(self):
        body = self.function("product_status_read")
        self.assertIn("atomic_load", body)
        self.assertNotRegex(body, r"k_mutex|k_sem|k_msgq|irq_lock|product_domain_")

    def test_the_owner_publishes_the_snapshot_with_an_atomic_store(self):
        self.assertIn("atomic_store", self.function("product_owner_entry"))

    def test_a_command_is_a_message_to_the_owner_queue_that_never_blocks(self):
        body = self.function("product_command_set_limit")
        self.assertIn("k_msgq_put", body)
        self.assertIn("K_NO_WAIT", body)
        self.assertNotRegex(body, r"product_domain_")
        self.assertIn("K_MSGQ_DEFINE(", self.shell)

    def test_the_owner_applies_messages_between_steps(self):
        body = self.function("product_owner_entry")
        self.assertIn("k_msgq_get", body)
        self.assertLess(body.index("k_msgq_get"), body.index("product_domain_step"))


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

    def test_guide_gives_each_delivery_line_its_own_branches(self):
        text = guide_section("### Vendor branch")
        for name in ("vendor-1.x", "work-1.x", "vendor-2.x", "work-2.x"):
            with self.subTest(name=name):
                self.assertIn(name, text)
        self.assertIn("never `vendor/1.x` beside a branch `vendor`", text)
        self.assertNotIn("git switch vendor\n", text)
        self.assertIn("work/1.0.1-acme1", GUIDE.read_text(), "a released SHA needs a durable ref")

    def test_guide_says_what_the_build_record_holds_of_a_delivery(self):
        text = guide_section("### Vendor branch")
        self.assertIn("holds only the commit", text)
        self.assertIn("git describe --tags --match 'vendor/*'", text)
        self.assertIn("not a count of local changes", text)
        self.assertRegex(text, r"only if it is a Zephyr module.*not built")

    def test_release_manifest_lists_deliveries_and_replaced_modules(self):
        row = next(line for line in guide_section("### Release bundle").splitlines()
                   if line.startswith("| `release-manifest.json`"))
        for words in ("delivery tag", "hash of its hash list", "replaced"):
            with self.subTest(words=words):
                self.assertIn(words, row)

    def test_bundle_row_gives_the_history_and_what_is_carried(self):
        section = guide_section("### Release bundle")
        row = next(line for line in section.splitlines() if line.startswith("| Carried-changes list"))
        self.assertIn("git log --no-merges <last-delivery-tag>..HEAD", row)
        self.assertIn("git diff <last-delivery-tag> HEAD", row)
        self.assertNotIn("command above", section)

    def test_guide_gives_the_steps_for_a_zephyr_patch_release(self):
        text = guide_section("## Upgrades and security fixes")
        for words in ("`MISMATCH`", "`crux-override`", "allowlist", "each module", "CRUX_REQUIRE_VERIFIED_ZEPHYR",
                      "reachable"):
            with self.subTest(words=words):
                self.assertIn(words, text)

    def test_guide_covers_a_product_that_imports_zephyr_only(self):
        text = guide_section("### Pinning by hand")
        self.assertIn("name-allowlist: [hal_nxp, <a module Crux did not allow>]", text)
        self.assertRegex(text, r"older Crux revision has no `west crux-pins`")

    def test_guide_states_the_patch_the_tls_backend_needs(self):
        bullet = next(line for line in GUIDE.read_text().splitlines() if "Optional groups are off" in line)
        self.assertIn("not stored in this repository", bullet)
        self.assertIn("Issue #185", bullet)
        self.assertIn("Issue #185", (REPO / "patches/zephyr/README.md").read_text())

    def test_sbom_section_covers_repairs_and_organisation_strings(self):
        text = guide_section("## SBOM")
        self.assertIn("each modified package", text)
        self.assertIn("module dependency records", text)
        self.assertIn("organisation string", text)

    def test_guide_says_a_reduction_removes_an_internal_purl(self):
        self.assertRegex(GUIDE.read_text(), r"carries the host and path.*reduction must remove it")

    def test_guide_calls_the_bundle_checks_a_schema_and_checker(self):
        text = GUIDE.read_text()
        self.assertGreaterEqual(text.count("schema"), 3)
        self.assertNotRegex(text, r"convention with no tool|A tool to assemble|No bundle, signing")

    def test_guide_points_at_the_provisional_schema_and_checker_in_this_repository(self):
        text = GUIDE.read_text()
        for path in ("release-bundle-checker.md", "schemas/release-manifest.schema.json",
                     "scripts/check-release-bundle.py"):
            with self.subTest(path=path):
                linked = re.findall(r"\]\(([^)#]*" + re.escape(path) + r")[^)]*\)", text)
                self.assertTrue(linked, path + " is named without a link")
                for target in linked:
                    self.assertTrue((GUIDE.parent / target).resolve().is_file(), target)
        section = guide_section("### Release bundle").lower()
        for words in ("provisional", "not frozen", "no product release", "assembler", "hashes only"):
            with self.subTest(words=words):
                self.assertIn(words, section)
        self.assertIn("provisional schema and checker", text[:1200].lower())

    def test_guide_no_longer_calls_the_bundle_a_convention_without_a_checker(self):
        text = GUIDE.read_text()
        for stale in ("is a convention", "adopted neither", "No adopted bundle schema or checker",
                      "exist in a product repository", "comes at the first product release"):
            with self.subTest(stale=stale):
                self.assertNotIn(stale, text)

    def test_guide_gives_no_number_of_years_of_support(self):
        self.assertNotRegex(GUIDE.read_text(), r"(?i)\b(\d+|one|two|three|five|ten)[- ]years?\b")

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

    def test_release_flow_settles_the_west_configuration_before_the_capture(self):
        block = guide_commands("--capture-inputs")
        self.assertLess(block.index("west crux-pins"), block.index("--capture-inputs"))
        bullet = next((line for line in guide_section("## Release").splitlines()
                       if line.startswith("- **West configuration.**")), "")
        for words in (".west/config", "`zephyr.base`", "extension", "before the capture",
                      "west config zephyr.base zephyr"):
            with self.subTest(words=words):
                self.assertIn(words, bullet)


def release_text():
    return RELEASE.read_text()


def release_section(heading):
    """Return one `## ` section of the release template, without its heading line."""
    lines = []
    inside = False
    fenced = False
    for line in release_text().splitlines():
        if line.startswith("```"):
            fenced = not fenced
        if not fenced and line.startswith("## "):
            if inside:
                break
            inside = line == f"## {heading}"
        elif inside:
            lines.append(line)
    if not inside:
        raise AssertionError(f"RELEASE.md has no section {heading!r}")
    return "\n".join(lines)


def table_header(text):
    """Return the cell names of the first Markdown table in a text."""
    for line in text.splitlines():
        if line.startswith("|"):
            return [cell.strip() for cell in line.strip().strip("|").split("|")]
    raise AssertionError("no table")


SECTIONS = (
    "Release identity",
    "Dependency inventory",
    "Modified upstreams",
    "Support period",
    "Release steps",
    "Bundle contents",
    "Release revision",
    "Checks run",
    "Vulnerability check",
)
# Names that a neutral sample must not carry: an employer, a commercial product or edition,
# a vendor SDK, or a private or consumer repository.
FORBIDDEN_NAMES = (
    "codesys", "wolfssl", "wolfssh", "nxp", "mcuxpresso", "datalight", "reliance",
    "crux-controller", "crux-codesys", "bitbucket", "employer",
)
ALLOWED_URL_PREFIXES = ("https://github.com/wonny-labs/crux-pilot/", "https://git.example/")


class ReleaseTemplateTest(unittest.TestCase):
    """The release record that a product copies, fills in and keeps with a release."""

    def setUp(self):
        self.text = release_text()

    def test_the_template_exists_and_the_readme_lists_and_links_it(self):
        readme = (TEMPLATE / "README.md").read_text()
        self.assertTrue(RELEASE.is_file())
        self.assertIn("](RELEASE.md)", readme)
        self.assertRegex(readme, r"(?m)^\| `RELEASE.md` \|")

    def test_it_has_each_required_section(self):
        headings = re.findall(r"(?m)^## (.+)$", self.text)
        for section in SECTIONS:
            with self.subTest(section=section):
                self.assertTrue(any(heading.startswith(section) for heading in headings), headings)
        self.assertRegex(self.text, r"(?m)^# Release record")

    def test_it_never_links_a_relative_path_that_a_copy_would_break(self):
        self.assertNotRegex(self.text, r"\]\((?!https://|#)")

    def test_the_inventory_asks_for_revision_licence_and_an_optional_end_of_support(self):
        section = release_section("Dependency inventory")
        header = table_header(section)
        for column in ("Project", "Pinned revision", "Licence"):
            with self.subTest(column=column):
                self.assertIn(column, header)
        self.assertTrue(any(re.match(r"End of support \(optional\)", cell) for cell in header), header)
        self.assertIn("every", section.lower())
        self.assertIn("west.frozen.yml", section)

    def test_the_inventory_leaves_the_end_of_support_to_the_upstream_and_the_product(self):
        section = release_section("Dependency inventory")
        self.assertNotRegex(section, r"(?i)\b(years?|months?)\b")
        self.assertNotRegex(section, r"\b\d{4}-\d{2}-\d{2}\b", "no example date")

    def test_modified_upstreams_name_the_guides_routes_and_ask_why(self):
        section = release_section("Modified upstreams")
        guide = GUIDE.read_text().lower()
        routes = ("vendor branch", "commercial edition", "soft fork", "west patch",
                  "binary-only", "outside the checkout")
        for route in routes:
            with self.subTest(route=route):
                self.assertIn(route, section.lower())
                self.assertIn(route, guide, "the template must use the guide's own route names")
        header = table_header(section)
        for column in ("Project", "Route", "Why"):
            with self.subTest(column=column):
                self.assertTrue(any(cell.startswith(column) for cell in header), header)
        self.assertIn("provisional", section.lower())
        self.assertRegex(section, r"(?i)nothing (is )?modified")
        self.assertRegex(section, r"(?i)short-term development check")

    def test_the_support_period_is_a_field_the_product_sets_with_no_figure(self):
        section = release_section("Support period")
        self.assertRegex(section, r"(?m)^- Support period: `<[^>]+>`$")
        self.assertRegex(section, r"(?i)the product sets")
        self.assertRegex(section, r"(?i)crux (does not|sets no)")
        self.assertNotRegex(section, r"\d", "no digit may sit in the support-period section")

    def test_no_support_period_figure_appears_anywhere(self):
        self.assertNotRegex(self.text, r"(?i)\b(years?|yrs?)\b")
        self.assertNotRegex(self.text, r"(?i)\b(one|two|three|four|five|six|seven|eight|nine|ten|\d+)[- ]+(years?|months?|yrs?)\b")
        self.assertNotRegex(self.text, r"(?i)\b\d+\s*(y|yr|yrs|mo|months?|years?)\b")
        for line in self.text.splitlines():
            if re.search(r"(?i)support period", line):
                with self.subTest(line=line):
                    self.assertNotRegex(line, r"\d")

    def test_no_owner_field_is_filled_in(self):
        fields = re.findall(r"(?im)^[-*|]\s*(?:owner|support owner|decided by|responsible)[^:\n]*:\s*(.*)$", self.text)
        self.assertTrue(fields, "the support section needs a decided-by field")
        for value in fields:
            with self.subTest(value=value):
                self.assertRegex(value.strip(), r"^`<[^>]+>`$")

    def test_release_steps_link_the_guide_at_a_pinned_revision_placeholder(self):
        urls = re.findall(r"https://\S*product-composition\.md\S*?(?=[`)\s]|$)", self.text)
        self.assertTrue(urls)
        for url in urls:
            with self.subTest(url=url):
                self.assertTrue(url.startswith("https://github.com/wonny-labs/crux-pilot/blob/<crux-revision>/"), url)
        self.assertNotRegex(self.text, r"/(blob|tree)/(main|master|HEAD)\b")
        self.assertIn("## Release", GUIDE.read_text().splitlines())
        self.assertIn("product-composition.md#release`", self.text)
        section = release_section("Release steps")
        self.assertRegex(section, r"(?i)moving branch")
        self.assertRegex(section, r"(?m)^1\. ")

    def test_release_steps_use_commands_the_guide_gives(self):
        section = release_section("Release steps")
        guide = GUIDE.read_text()
        for command in ("west crux-pins", "west manifest --freeze --active-only",
                        "-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON", "record-build-provenance.py"):
            with self.subTest(command=command):
                self.assertIn(command, section)
                self.assertIn(command, guide)

    def test_release_steps_keep_the_guides_order_around_the_build(self):
        steps = release_section("Release steps")
        order = ["west crux-pins", "west manifest --freeze --active-only", "--capture-inputs",
                 "west spdx --init", "-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON", "using the captured inputs",
                 "`west spdx`", "check-release-bundle", "checks below"]
        positions = []
        for marker in order:
            with self.subTest(marker=marker):
                self.assertIn(marker, steps)
            positions.append(steps.find(marker))
        self.assertEqual(sorted(positions), positions)
        self.assertRegex(steps, r"(?i)mirror rewrites off")

    def test_release_steps_settle_the_west_configuration_before_the_capture(self):
        steps = release_section("Release steps")
        order = ["`west crux-pins` again", "`zephyr.base`", "west config zephyr.base zephyr", "--capture-inputs"]
        positions = []
        for marker in order:
            with self.subTest(marker=marker):
                self.assertIn(marker, steps)
            positions.append(steps.find(marker))
        self.assertEqual(sorted(positions), positions)
        self.assertIn(".west/config", steps)

    def test_the_bundle_may_hold_hashes_only_and_a_public_repository_must(self):
        section = release_section("Bundle contents")
        lowered = section.lower()
        self.assertIn("hashes only", lowered)
        for artefact in ("`.elf`", "`.map`", "image"):
            with self.subTest(artefact=artefact):
                self.assertIn(artefact, lowered)
        self.assertRegex(lowered, r"public product repository")
        self.assertRegex(lowered, r"never[^.\n]*release asset")
        self.assertIn("sha-256", lowered)

    def test_the_bundle_table_says_what_goes_outward_and_what_stays_internal(self):
        section = release_section("Bundle contents")
        header = table_header(section)
        self.assertIn("Item", header)
        self.assertIn("Audience", header)
        self.assertTrue(any(cell.startswith("In this bundle") for cell in header), header)
        self.assertRegex(section, r"\| Outward")
        self.assertRegex(section, r"\| Internal")
        for item in ("Firmware image", "licences/", "release-manifest.json", "Build record",
                     "Lock file", "Pin report", "Full SBOM", "Debug files"):
            with self.subTest(item=item):
                self.assertIn(item, section)

    def test_the_schema_and_checker_are_named_by_existing_paths_and_called_provisional(self):
        paths = set(re.findall(r"\b((?:docs|scripts)/[A-Za-z0-9_./-]*[A-Za-z0-9_])", self.text))
        self.assertGreaterEqual(paths, {"docs/schemas/release-manifest.schema.json",
                                        "scripts/check-release-bundle.py"})
        for path in paths:
            with self.subTest(path=path):
                self.assertTrue((REPO / path).exists(), f"{path} does not exist in the repository")
        paragraphs = [p for p in self.text.split("\n\n") if "release-manifest.schema.json" in p]
        self.assertTrue(paragraphs)
        for paragraph in paragraphs:
            with self.subTest(paragraph=paragraph[:40]):
                self.assertIn("provisional", paragraph.lower())
                self.assertIn("not frozen", paragraph.lower())

    def test_the_release_revision_section_records_each_identity(self):
        section = release_section("Release revision")
        for field in ("Release tag", "Product commit", "Crux revision", "Zephyr revision",
                      "Image SHA-256", "Firmware identity"):
            with self.subTest(field=field):
                self.assertRegex(section, rf"(?m)^- {field}: `<[^>]+>`")
        self.assertRegex(section, r"(?i)cannot name its own commit")

    def test_the_checks_section_records_command_result_who_and_what_was_skipped(self):
        section = release_section("Checks run")
        header = table_header(section)
        for column in ("Check", "Command", "Result", "Run by"):
            with self.subTest(column=column):
                self.assertIn(column, header)
        self.assertRegex(section, r"(?i)skipped")
        self.assertRegex(section, r"(?i)not run|did not run")

    def test_the_vulnerability_check_is_a_marked_placeholder_with_no_procedure(self):
        section = release_section("Vulnerability check")
        self.assertRegex(section, r"(?i)not defined yet")
        self.assertRegex(section, r"(?i)placeholder")
        self.assertIn("SBOM", section)
        self.assertNotIn("```", section)
        self.assertNotRegex(section, r"(?m)^\s*(\d+\.|- \[ \])", "no steps: none is defined")
        self.assertNotRegex(section, r"(?i)\b(grype|trivy|osv|cve-|nvd|dependency-check)\b")

    def test_the_template_names_no_employer_commercial_product_or_private_repository(self):
        lowered = self.text.lower()
        for name in FORBIDDEN_NAMES:
            with self.subTest(name=name):
                self.assertNotIn(name, lowered)
        for url in re.findall(r"https?://[^\s`)>]+", self.text):
            with self.subTest(url=url):
                self.assertTrue(url.startswith(ALLOWED_URL_PREFIXES), url)
        self.assertNotRegex(self.text, r"git@|ssh://")

    def test_the_template_leaves_the_manifest_alone(self):
        self.assertNotIn("RELEASE.md", MANIFEST.read_text())


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

    def test_reliance_edge_stays_in_its_optional_group(self):
        # Without its group the project would drop out of the check that optional stacks stay
        # off by default, and an ordinary workspace would fetch the GPLv2 source.
        projects = {entry["name"]: entry for entry in self.crux["projects"]}
        self.assertEqual(["crux-reliance"], projects["reliance-edge"].get("groups"))
        self.assertIn("-crux-reliance", self.crux["group-filter"])


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
        # West writes bytecode beside the command, which the real repository ignores.
        cls.older_crux = cls.commit(directory, {"west.yml": crux_text, ".gitignore": (REPO / ".gitignore").read_text()},
                                    "crux manifest, before the command")
        shutil.copytree(REPO / "scripts/west_commands", directory / "scripts/west_commands",
                        ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy(REPO / "scripts/west-commands.yml", directory / "scripts/west-commands.yml")
        cls.crux = cls.commit(directory, {}, "crux pin command")
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
        # Only the groups the examples enable are fetched. A group they leave off stays absent.
        enabled = {group.lstrip("+") for group in manifest["group-filter"]}
        for entry in self.optional:
            with self.subTest(name=entry["name"]):
                if enabled & set(entry["groups"]):
                    self.assertIn(f"OK {entry['name']} ", output)
                else:
                    self.assertNotIn(f"OK {entry['name']} ", output)

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

    def released(self, source):
        """Freeze a workspace, tag the lock on a release branch and build a fresh workspace from it."""
        frozen = self.west("manifest", "--freeze", "--active-only", cwd=source, check=True).stdout
        product = source / "product-x"
        git(product, "switch", "-q", "-c", "release/1.0", env=self.env)
        self.commit(product, {"locks/west.frozen.yml": frozen}, "build: lock the manifest")
        git(product, "tag", "-a", "product-x-1.0.0", "-m", "product-x 1.0.0", env=self.env)

        fresh = Path(tempfile.mkdtemp(dir=self.root)) / "release"
        self.west("init", "-m", product.as_uri(), "--mr", "product-x-1.0.0",
                  "--mf", "locks/west.frozen.yml", str(fresh), cwd=self.root, check=True)
        self.west("update", cwd=fresh, check=True)
        return fresh

    def captured_inputs(self, fresh):
        """Run the build record's input capture on a frozen workspace."""
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
        return json.loads(inputs.read_text())

    def test_a_release_runs_from_a_frozen_manifest_in_a_fresh_workspace(self):
        source = self.workspace()
        plain = self.west("manifest", "--freeze", cwd=source)
        self.assertNotEqual(0, plain.returncode, "an optional project is declared and not cloned")
        fresh = self.released(source)
        self.assertIn(f"OK zephyr {self.zephyr[:12]}", self.assert_pins_pass(fresh))
        captured = self.captured_inputs(fresh)
        self.assertFalse(captured["source"]["dirty"], captured["source"]["git_status_counts"])
        commits = {entry["name"]: entry["git_commit"] for entry in captured["west_projects"]}
        self.assertEqual(self.zephyr, commits["zephyr"])

    @staticmethod
    def recorder():
        spec = importlib.util.spec_from_file_location(
            "record_build_provenance", REPO / "scripts/record-build-provenance.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def inputs(self, fresh):
        """Return the inputs that the build record captures, as its own code computes them."""
        with mock.patch.dict(os.environ, self.env, clear=True):
            return self.recorder().workspace_inputs(fresh / "product-x", fresh)

    def accepts(self, captured, fresh):
        """Run the check that the build record makes between the capture and the record."""
        with mock.patch.dict(os.environ, self.env, clear=True):
            self.recorder().require_matching_inputs(captured, self.inputs(fresh))

    def test_west_writes_zephyr_base_at_the_first_extension_command_and_not_before(self):
        fresh = self.released(self.workspace())
        config = fresh / ".west/config"
        self.assertNotIn("[zephyr]", config.read_text())
        self.west("list", cwd=fresh, check=True)
        self.assertNotIn("[zephyr]", config.read_text(), "a built-in command must not write it")
        self.west("crux-pins", cwd=fresh, check=True)
        self.assertIn("[zephyr]\nbase = zephyr\n", config.read_text())

    def test_a_capture_before_the_first_extension_command_is_refused_at_record_time(self):
        fresh = self.released(self.workspace())
        captured = self.inputs(fresh)
        self.west("crux-pins", cwd=fresh, check=True)
        with self.assertRaisesRegex(ValueError, "inputs changed during verification"):
            self.accepts(captured, fresh)
        changed = self.inputs(fresh)
        self.assertEqual(["west_workspace_config_sha256"], [key for key in captured if captured[key] != changed[key]])

    def test_a_capture_after_crux_pins_is_accepted_after_a_later_extension_command(self):
        fresh = self.released(self.workspace())
        self.west("crux-pins", cwd=fresh, check=True)
        captured = self.inputs(fresh)
        self.west("crux-pins", cwd=fresh, check=True)
        self.accepts(captured, fresh)

    def test_west_config_writes_the_file_that_the_extension_command_writes(self):
        by_command = self.released(self.workspace())
        self.west("crux-pins", cwd=by_command, check=True)
        by_config = self.released(self.workspace())
        self.west("config", "zephyr.base", "zephyr", cwd=by_config, check=True)
        captured = self.inputs(by_config)
        self.west("crux-pins", cwd=by_config, check=True)
        self.accepts(captured, by_config)
        self.assertEqual((by_command / ".west/config").read_bytes(), (by_config / ".west/config").read_bytes())

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

    def deliver(self, repository, version, files, branch="vendor-1.x"):
        """Import a delivery as received, as the guide says, so what it dropped goes too."""
        git(repository, "switch", "-q", branch, env=self.env)
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
        git(repository, "switch", "-q", "-c", "vendor-1.x", env=self.env)
        self.commit_delivery(repository, "1.0.0", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.0.0\n",
                                                   "src/old.c": "old\n", ".gitignore": "*.a\n",
                                                   "lib/vendor.a": "binary 1.0.0\n"})
        git(repository, "switch", "-q", "-c", "work-1.x", env=self.env)
        self.commit(repository, {"src/a.c": "a 1.0.0\nlocal fix\n"}, "[acme noup] keep the local fix")
        delivery = {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.1.0\n", "src/new.c": "new\n",
                    ".gitignore": "*.a\n", "lib/vendor.a": "binary 1.1.0\n"}
        self.deliver(repository, "1.1.0", delivery)
        self.assertEqual(sorted(delivery), sorted(git(
            repository, "ls-tree", "-r", "--name-only", "vendor/1.1.0", env=self.env).splitlines()))
        git(repository, "switch", "-q", "work-1.x", env=self.env)
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

    def hand_pinned(self, zephyr=None, commands=True, name="zephyr", modules=None, crux=None, zephyr_import=None):
        """Return a product manifest that pins each project itself, importing at most Zephyr."""
        modules = [self.allowlist[0], OUTSIDE] if modules is None else modules
        pinned = "".join(f"    - name: {module}\n      remote: zephyrproject-rtos\n"
                         f"      revision: {self.modules[module]}\n      path: modules/{module}\n"
                         for module in modules)
        west_commands = "      west-commands: scripts/west-commands.yml\n" if commands else ""
        allowlist = (f"      import:\n        name-allowlist: [{', '.join(zephyr_import)}]\n"
                     if zephyr_import else "")
        return ("manifest:\n"
                '  version: "1.2"\n'
                "  remotes:\n"
                f"    - name: crux\n      url-base: {self.remote_base()}\n"
                f"    - name: zephyrproject-rtos\n      url-base: {PUBLIC_BASE}zephyrproject-rtos\n"
                "  projects:\n"
                f"    - name: crux-pilot\n      remote: crux\n      revision: {crux or self.crux}\n"
                f"{west_commands}"
                f"    - name: {name}\n      remote: zephyrproject-rtos\n      repo-path: zephyr\n"
                f"      revision: {zephyr or self.zephyr}\n"
                f"{allowlist}"
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

    def line_repository(self, name):
        """Start a vendor repository: delivery 1.0.0 on line 1.x, with one local change."""
        repository = self.root / f"internal/scm/acme/{name}"
        repository.mkdir(parents=True)
        git(repository, "init", "-q", env=self.env)
        git(repository, "switch", "-q", "-c", "vendor-1.x", env=self.env)
        self.commit_delivery(repository, "1.0.0", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.0.0\n"})
        git(repository, "switch", "-q", "-c", "work-1.x", env=self.env)
        self.commit(repository, {"src/a.c": "a 1.0.0\nlocal fix\n"}, "[acme noup] keep the local fix")
        return repository

    def merge(self, repository, branch, tag):
        git(repository, "switch", "-q", branch, env=self.env)
        git(repository, "merge", "-q", "--no-edit", tag, env=self.env)
        return git(repository, "rev-parse", "HEAD", env=self.env)

    def test_a_fix_delivery_stays_on_its_own_line(self):
        repository = self.line_repository("vendor-lines")
        git(repository, "switch", "-q", "-c", "vendor-2.x", "vendor/1.0.0", env=self.env)
        self.deliver(repository, "2.0.0", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 2.0.0\n", "src/c.c": "c 2.0.0\n"},
                     branch="vendor-2.x")
        git(repository, "switch", "-q", "-c", "work-2.x", "work-1.x", env=self.env)
        git(repository, "merge", "-q", "--no-edit", "vendor/2.0.0", env=self.env)
        line_two = git(repository, "rev-parse", "HEAD", env=self.env)

        self.deliver(repository, "1.0.1", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.0.1\n"})
        released = self.merge(repository, "work-1.x", "vendor/1.0.1")
        git(repository, "tag", "-a", "work/1.0.1-acme1", "-m", "released", released, env=self.env)

        # Line 2 is untouched.
        self.assertEqual(line_two, git(repository, "rev-parse", "work-2.x", env=self.env))
        self.assertEqual("b 2.0.0", git(repository, "show", "work-2.x:src/b.c", env=self.env))
        ancestor = subprocess.run([GIT, "-C", str(repository), "merge-base", "--is-ancestor",
                                   "vendor/1.0.1", "work-2.x"], env=self.env)
        self.assertEqual(1, ancestor.returncode)
        self.assertRegex(git(repository, "describe", "--tags", "--match", "vendor/*", "work-2.x", env=self.env),
                         r"^vendor/2\.0\.0-\d+-g[0-9a-f]+$")

        # A product pinned to line 1 passes, and the local change survives there.
        manifest = self.internal(self.entry("vendor-lines", released, "modules/vendor-lines"))
        workspace = self.workspace(manifest)
        self.assertRegex(self.assert_pins_pass(workspace), rf"(?m)^PRODUCT vendor-lines {released[:12]}$")
        checkout = workspace / "modules/vendor-lines"
        self.assertEqual("a 1.0.0\nlocal fix\n", (checkout / "src/a.c").read_text())
        self.assertEqual("b 1.0.1\n", (checkout / "src/b.c").read_text())
        self.assertFalse((checkout / "src/c.c").exists())

        # The tag on the released SHA is outside vendor/*, so describe still names the delivery.
        # The number counts the local commit and the merge, not local changes.
        self.assertRegex(git(checkout, "describe", "--tags", "--match", "vendor/*", "HEAD", env=self.env),
                         r"^vendor/1\.0\.1-2-g[0-9a-f]+$")
        self.assertEqual("vendor/1.0.1", git(checkout, "describe", "--tags", "--abbrev=0", "--match", "vendor/*",
                                             "HEAD", env=self.env))

        # With its branches gone, the durable ref alone keeps the released SHA fetchable.
        git(repository, "switch", "-q", "vendor-2.x", env=self.env)
        git(repository, "branch", "-q", "-D", "work-1.x", "vendor-1.x", env=self.env)
        self.assert_pins_pass(self.workspace(manifest))

    def test_a_line_branch_cannot_sit_under_a_branch_of_that_name(self):
        repository = Path(tempfile.mkdtemp(dir=self.root))
        git(repository, "init", "-q", env=self.env)
        self.commit(repository, {"a.c": "a"}, "first")
        git(repository, "branch", "vendor", env=self.env)
        refused = subprocess.run([GIT, "-C", str(repository), "branch", "vendor/1.x"], env=self.env,
                                 capture_output=True, text=True)
        self.assertNotEqual(0, refused.returncode)
        git(repository, "branch", "vendor-1.x", env=self.env)
        git(repository, "branch", "vendor-2.x", env=self.env)

    def test_a_fix_delivery_on_one_linear_branch_drags_the_tree_back(self):
        repository = self.line_repository("vendor-linear")
        git(repository, "branch", "-q", "-m", "vendor-1.x", "vendor", env=self.env)
        self.deliver(repository, "2.0.0", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 2.0.0\n", "src/c.c": "c 2.0.0\n"},
                     branch="vendor")
        self.merge(repository, "work-1.x", "vendor/2.0.0")
        self.deliver(repository, "1.0.1", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.0.1\n"}, branch="vendor")
        self.merge(repository, "work-1.x", "vendor/1.0.1")
        self.assertEqual("b 1.0.1\n", (repository / "src/b.c").read_text())
        self.assertFalse((repository / "src/c.c").exists())

    def test_the_build_record_holds_a_vendor_project_by_commit_only(self):
        repository = self.line_repository("vendor-record")
        self.deliver(repository, "1.1.0", {"src/a.c": "a 1.0.0\n", "src/b.c": "b 1.1.0\n"})
        merge = self.merge(repository, "work-1.x", "vendor/1.1.0")
        fresh = self.released(self.workspace(self.internal(
            self.entry("vendor-record", merge, "modules/vendor-record"))))
        self.assertRegex(self.assert_pins_pass(fresh), rf"(?m)^PRODUCT vendor-record {merge[:12]}$")
        captured = self.captured_inputs(fresh)
        project = next(entry for entry in captured["west_projects"] if entry["name"] == "vendor-record")
        self.assertEqual(merge, project["git_commit"])
        self.assertIs(False, project["dirty"])
        self.assertNotIn("vendor/1.", json.dumps(captured), "the record names no delivery")
        self.assertEqual("vendor/1.1.0", git(fresh / "modules/vendor-record", "describe", "--tags", "--abbrev=0",
                                             "--match", "vendor/*", "HEAD", env=self.env))

    def test_a_change_the_next_delivery_absorbs_stays_in_the_log_and_leaves_the_diff(self):
        repository = self.line_repository("vendor-absorbed")
        self.deliver(repository, "1.1.0", {"src/a.c": "a 1.0.0\nlocal fix\n", "src/b.c": "b 1.1.0\n"})
        self.merge(repository, "work-1.x", "vendor/1.1.0")
        self.assertEqual("[acme noup] keep the local fix",
                         git(repository, "log", "--no-merges", "--format=%s", "vendor/1.1.0..HEAD", env=self.env))
        self.assertEqual("", git(repository, "diff", "--name-only", "vendor/1.1.0", "HEAD", env=self.env))

    def run_guide_commands(self, marker, directory):
        return subprocess.run(["sh", "-c", guide_commands(marker)], cwd=directory, env=self.env,
                              capture_output=True, text=True)

    def import_with_hash_list(self, files, extra_in_repository=None, add_options=(), link=None):
        """Hash a delivery as received, import it, and return the output of the guide's check."""
        top = Path(tempfile.mkdtemp(dir=self.root))
        received = top / "received"
        repository = top / "repository"
        received.mkdir()
        repository.mkdir()
        for name, content in files.items():
            for base in (received, repository):
                path = base / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            if link:
                for base in (received, repository):
                    (base / link).symlink_to(name)
        self.assertEqual(0, self.run_guide_commands("sort -k 2 > ../delivery-1.1.0.sha256", received).returncode)
        for name, content in (extra_in_repository or {}).items():
            (repository / name).write_bytes(content)
        git(repository, "init", "-q", env=self.env)
        git(repository, *add_options, "add", "-A", "-f", env=self.env)
        git(repository, "commit", "-q", "-m", "vendor: import delivery 1.1.0", env=self.env)
        git(repository, "tag", "-a", "vendor/1.1.0", "-m", "delivery 1.1.0", env=self.env)
        return self.run_guide_commands("git cat-file blob", repository)

    def test_the_hash_list_check_finds_stray_files_and_changed_line_endings(self):
        files = {"src/a.c": b"a\r\nb\r\n", "src/with space.c": b"c\n", "src/caf\u00e9.c": b"d\n",
                 "lib/vendor.a": b"\x00\x01"}
        clean = self.import_with_hash_list(files)
        self.assertEqual((0, ""), (clean.returncode, clean.stdout), clean.stderr)

        stray = self.import_with_hash_list(files, extra_in_repository={"build.log": b"left behind\n"})
        self.assertNotEqual(0, stray.returncode)
        self.assertIn("build.log", stray.stdout)

        changed = self.import_with_hash_list(files, add_options=("-c", "core.autocrlf=input"))
        self.assertNotEqual(0, changed.returncode)
        self.assertIn("src/a.c", changed.stdout)

    def test_the_hash_list_check_shows_a_symlink_as_a_difference(self):
        result = self.import_with_hash_list({"a.c": b"a\n"}, link="link.c")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("link.c", result.stdout)

    def module_entry(self, name, revision, extra=""):
        return (f"    - name: {name}\n      url: {PUBLIC_BASE}zephyrproject-rtos/{name}\n"
                f"      revision: {revision}\n      path: modules/{name}\n{extra}")

    def test_a_zephyr_patch_release_needs_overrides_for_zephyr_and_each_moved_module(self):
        moved, kept = self.allowlist[:2]
        new_module = self.commit(self.root / f"public/zephyrproject-rtos/{moved}", {"m.txt": "moved"}, "module moved")
        zephyr = self.root / "public/zephyrproject-rtos/zephyr"
        git(zephyr, "switch", "-q", "-c", "v4.4-branch", self.zephyr, env=self.env)
        text = (zephyr / "west.yml").read_text().replace(self.modules[moved], new_module)
        newer = self.commit(zephyr, {"west.yml": text}, "zephyr patch release")
        git(zephyr, "switch", "-q", "main", env=self.env)

        reason = '      userdata:\n        crux-override: "zephyr patch release"\n'

        def manifest(zephyr_extra, module=""):
            entry = (f"    - name: zephyr\n      url: {self.zephyr_url}\n      revision: {newer}\n"
                     f"      import:\n        name-allowlist: [{', '.join(self.allowlist)}]\n{zephyr_extra}{module}")
            return MANIFEST.read_text().replace("      import: true\n", "      import: true\n" + entry)

        plain = self.west("crux-pins", cwd=self.workspace(manifest("")))
        self.assertNotEqual(0, plain.returncode, plain.stdout + plain.stderr)
        self.assertIn(f"MISMATCH zephyr {newer[:12]} (contract {self.zephyr[:12]})", plain.stdout)
        self.assertIn(f"MISMATCH {moved} {new_module[:12]} (contract {self.modules[moved][:12]})", plain.stdout)

        declared = self.west("crux-pins", cwd=self.workspace(manifest(reason)))
        self.assertNotEqual(0, declared.returncode, declared.stdout + declared.stderr)
        self.assertIn(f"OVERRIDE zephyr {newer[:12]} (contract {self.zephyr[:12]}): zephyr patch release",
                      declared.stdout)
        self.assertIn(f"MISMATCH {moved} {new_module[:12]}", declared.stdout)

        output = self.assert_pins_pass(self.workspace(manifest(reason, self.module_entry(moved, new_module, reason))))
        self.assertIn(f"OVERRIDE zephyr {newer[:12]}", output)
        self.assertIn(f"OVERRIDE {moved} {new_module[:12]} (contract {self.modules[moved][:12]}): zephyr patch release",
                      output)
        self.assertIn(f"OK {kept} {self.modules[kept][:12]}", output)

    def test_a_moved_pin_needs_the_contract_commit_reachable_and_narrow_needs_an_ancestor(self):
        name = self.allowlist[2]
        module = self.root / f"public/zephyrproject-rtos/{name}"
        git(module, "switch", "-q", "--orphan", "sideways", env=self.env)
        sideways = self.commit(module, {"m.txt": "sideways"}, "not a descendant of the contract commit")
        git(module, "switch", "-q", "main", env=self.env)
        reason = '      userdata:\n        crux-override: "moved"\n'
        manifest = MANIFEST.read_text().replace(
            "      import: true\n", "      import: true\n" + self.module_entry(name, sideways, reason))

        output = self.assert_pins_pass(self.workspace(manifest))
        self.assertIn(f"OVERRIDE {name} {sideways[:12]}", output)

        workspace = self.prepared(manifest)
        self.west("update", "--narrow", cwd=workspace, check=True)
        narrow = self.west("crux-pins", cwd=workspace)
        self.assertNotEqual(0, narrow.returncode, narrow.stdout + narrow.stderr)
        self.assertIn(f"{name}: cannot resolve the contract revision", narrow.stderr)

        # Narrow is fine when the contract commit is an ancestor of the new pin.
        descendant = self.commit(module, {"m.txt": "descendant"}, "descendant of the contract commit")
        workspace = self.prepared(MANIFEST.read_text().replace(
            "      import: true\n", "      import: true\n" + self.module_entry(name, descendant, reason)))
        self.west("update", "--narrow", cwd=workspace, check=True)
        self.assertIn(f"OVERRIDE {name} {descendant[:12]}", self.assert_pins_pass(workspace))

    def test_a_product_that_imports_only_zephyr_passes_while_its_pins_match(self):
        chosen = [self.allowlist[0], OUTSIDE]
        workspace = self.workspace(self.hand_pinned(modules=[], zephyr_import=chosen))
        output = self.assert_pins_pass(workspace)
        self.assertRegex(output, rf"(?m)^CONTRACT crux-pilot {self.crux[:12]}$")
        self.assertIn(f"OK zephyr {self.zephyr[:12]}", output)
        self.assertIn(f"OK {self.allowlist[0]} {self.modules[self.allowlist[0]][:12]}", output)
        self.assertIn(f"OK {OUTSIDE} {self.modules[OUTSIDE][:12]}", output)
        self.assertNotIn(self.allowlist[1], output)
        names = self.active_names(workspace)
        for entry in self.optional:
            with self.subTest(name=entry["name"]):
                self.assertNotIn(entry["name"], names)

    def test_a_product_that_imports_only_zephyr_on_another_zephyr_is_a_mismatch(self):
        manifest = self.hand_pinned(self.other_zephyr, modules=[], zephyr_import=[self.allowlist[0], OUTSIDE])
        result = self.west("crux-pins", cwd=self.workspace(manifest))
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn(f"MISMATCH zephyr {self.other_zephyr[:12]} (contract {self.zephyr[:12]})", result.stdout)
        self.assertIn(f"OK {OUTSIDE} {self.modules[OUTSIDE][:12]}", result.stdout)

    def test_an_older_crux_revision_has_no_pin_command(self):
        manifest = self.hand_pinned(crux=self.older_crux, modules=[], zephyr_import=[self.allowlist[0]])
        result = self.west("crux-pins", cwd=self.workspace(manifest))
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("crux-pins", result.stderr)
        self.assertNotIn("CONTRACT", result.stdout)


if __name__ == "__main__":
    unittest.main()
