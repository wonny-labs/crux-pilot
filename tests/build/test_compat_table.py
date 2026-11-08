# SPDX-License-Identifier: Apache-2.0
"""Tests of scripts/gen-compat-table.py, the compatibility table generator (Refs #301).

The table is derived from a west manifest and from nothing else. These tests hold that: a golden table
for an invented manifest, the structure of the table for the repository's own west.yml (no commit is
spelled out here, because the pins move), the refusals, the determinism and the marking of optional
groups. Nothing here needs west, git or the network. The cases that parse a manifest need PyYAML, which
west installs and the host CI job does not, so they skip without it, as in test_product_template.py.
"""
import ast
import contextlib
import importlib.util
import io
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

try:
    import yaml
except ImportError:
    yaml = None

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts/gen-compat-table.py"
FIXTURES = Path(__file__).resolve().parent / "fixtures/compat_table"
MANIFEST = FIXTURES / "manifest.yml"
EXPECTED = FIXTURES / "expected.md"
GUIDE = REPO / "docs/compatibility-table.md"
REQUIRED = ("zephyr", "hal_nxp", "mbedtls", "tf-psa-crypto", "wolfssl", "wolfssh", "wolfpsa", "reliance-edge")
SHA = "0123456789abcdef0123456789abcdef01234567"
OTHER = "fedcba9876543210fedcba9876543210fedcba98"


def load_module():
    spec = importlib.util.spec_from_file_location("gen_compat_table", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(*arguments):
    """Run the script as a user would. Return (exit status, stdout, stderr)."""
    result = subprocess.run([sys.executable, str(SCRIPT), *arguments], capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr


def manifest_text(*projects, group_filter="[-crux-optional]", extra=""):
    """A manifest whose projects are the given YAML snippets, each one a list item without indent."""
    body = "".join("    " + "\n      ".join(p.strip("\n").splitlines()) + "\n" for p in projects)
    return f"manifest:\n  group-filter: {group_filter}\n{extra}  projects:\n{body}"


def project(name, revision=SHA, comment="", groups=(), extra=""):
    lines = [f"- name: {name}", f"revision: {revision}" + (f" # {comment}" if comment else "")]
    if groups:
        lines.append("groups:")
        lines += [f"  - {group}" for group in groups]
    if extra:
        lines.append(extra)
    return "\n".join(lines)


class Workspace:
    def __init__(self, test):
        self.directory = tempfile.TemporaryDirectory()
        test.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write(self, text, name="west.yml"):
        path = self.root / name
        path.write_text(text)
        return str(path)


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class GoldenTest(unittest.TestCase):
    def test_fixture_manifest_gives_the_golden_table(self):
        status, out, err = run("--manifest", str(MANIFEST), "--label", "v0.0.0-fixture")
        self.assertEqual((0, ""), (status, err))
        self.assertEqual(EXPECTED.read_text(), out)

    def test_every_project_of_the_fixture_appears_once_as_a_column(self):
        _, out, _ = run("--manifest", str(MANIFEST), "--label", "x")
        header = [cell.strip() for cell in out.splitlines()[0].strip("|").split("|")]
        names = [p["name"] for p in yaml.safe_load(MANIFEST.read_text())["manifest"]["projects"]]
        for name in names:
            self.assertEqual(1, header.count(name), name)
        self.assertEqual("Crux revision", header[0])

    def test_required_columns_come_first_in_the_documented_order_then_the_others_sorted(self):
        _, out, _ = run("--manifest", str(MANIFEST), "--label", "x")
        header = [cell.strip() for cell in out.splitlines()[0].strip("|").split("|")][1:]
        self.assertEqual(list(REQUIRED), header[:len(REQUIRED)])
        self.assertEqual(["dual-mod", "extra-mod"], header[len(REQUIRED):])

    def test_a_licence_is_never_taken_from_a_comment(self):
        _, out, _ = run("--manifest", str(MANIFEST), "--label", "x")
        self.assertNotIn("GPL", out)
        self.assertEqual(1, out.count("licence class"))  # only the declared one

    def test_the_label_is_the_first_cell_of_the_only_row(self):
        _, out, _ = run("--manifest", str(MANIFEST), "--label", "main@abc1234")
        rows = [line for line in out.splitlines() if line.startswith("| ")]
        self.assertEqual(3, len(rows))  # header, rule, one row
        self.assertTrue(rows[2].startswith("| main@abc1234 | "))


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class RealManifestTest(unittest.TestCase):
    """The repository's own west.yml. The commits are read back from it, not copied here."""

    def setUp(self):
        self.data = yaml.safe_load((REPO / "west.yml").read_text())["manifest"]
        self.status, self.out, self.err = run("--label", "main@test")

    def test_it_succeeds(self):
        self.assertEqual((0, ""), (self.status, self.err))

    def test_every_project_of_the_manifest_appears_as_a_column(self):
        header = [cell.strip() for cell in self.out.splitlines()[0].strip("|").split("|")]
        for entry in self.data["projects"]:
            self.assertIn(entry["name"], header)
        for name in REQUIRED:
            self.assertIn(name, header)

    def test_every_pinned_commit_appears_abbreviated_in_the_row(self):
        row = self.out.splitlines()[2]
        for entry in self.data["projects"]:
            self.assertIn(f"`{entry['revision'][:12]}`", row, entry["name"])
            self.assertNotIn(entry["revision"], self.out)

    def test_projects_in_an_off_by_default_group_are_marked_optional(self):
        header = [cell.strip() for cell in self.out.splitlines()[0].strip("|").split("|")]
        cells = [cell.strip() for cell in self.out.splitlines()[2].strip("|").split("|")]
        by_name = dict(zip(header, cells))
        off = {item[1:] for item in self.data["group-filter"] if item.startswith("-")}
        for entry in self.data["projects"]:
            marked = "optional" in by_name[entry["name"]]
            self.assertEqual(bool(entry.get("groups")) and set(entry["groups"]) <= off, marked, entry["name"])
        self.assertIn("optional", by_name["wolfssl"])
        self.assertNotIn("optional", by_name["zephyr"])

    def test_versions_come_from_the_trailing_comments(self):
        row = self.out.splitlines()[2]
        for version in ("v4.4.2", "v5.9.4-stable", "v1.6.0-stable", "v3.0"):
            self.assertIn(f"({version})", row)

    def test_the_imported_hal_is_not_given_a_commit(self):
        cells = [cell.strip() for cell in self.out.splitlines()[2].strip("|").split("|")]
        header = [cell.strip() for cell in self.out.splitlines()[0].strip("|").split("|")]
        hal = dict(zip(header, cells))["hal_nxp"]
        self.assertEqual("imported from `zephyr`, not pinned here", hal)

    def test_no_licence_is_claimed_for_the_real_manifest(self):
        self.assertNotIn("GPL", self.out)
        self.assertNotIn("licence", self.out)


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class RefusalTest(unittest.TestCase):
    def setUp(self):
        self.workspace = Workspace(self)

    def refuse(self, text, *needles, label="v0"):
        status, out, err = run("--manifest", self.workspace.write(text), "--label", label)
        self.assertEqual(1, status, err)
        self.assertEqual("", out)
        for needle in needles:
            self.assertIn(needle, err)
        self.assertNotIn("Traceback", err)

    def test_a_branch_name_is_refused_and_named(self):
        self.refuse(manifest_text(project("zephyr"), project("wolfssl", revision="main")),
                    "wolfssl", "main", "40-character")

    def test_a_tag_name_is_refused_even_when_it_is_the_comment_version(self):
        self.refuse(manifest_text(project("zephyr", revision="v4.4.2")), "zephyr", "v4.4.2", "40-character")

    def test_an_abbreviated_commit_is_refused(self):
        self.refuse(manifest_text(project("zephyr", revision=SHA[:12])), "zephyr", SHA[:12], "40-character")

    def test_a_missing_revision_is_refused(self):
        self.refuse(manifest_text("- name: zephyr\npath: zephyr"), "zephyr", "no revision")

    def test_the_default_revision_of_the_manifest_does_not_rescue_a_missing_one(self):
        text = manifest_text("- name: zephyr\npath: zephyr", extra="  defaults:\n    revision: main\n")
        self.refuse(text, "zephyr", "no revision")

    def test_a_default_commit_does_not_rescue_a_missing_revision_either(self):
        text = manifest_text("- name: zephyr\npath: zephyr", extra=f"  defaults:\n    revision: {SHA}\n")
        self.refuse(text, "zephyr", "no revision")

    def test_a_malformed_import_allowlist_entry_is_refused(self):
        text = manifest_text(project("zephyr", extra="import:\n  name-allowlist: ['a`b']"))
        self.refuse(text, "zephyr", "name-allowlist")

    def test_a_non_string_revision_is_refused(self):
        self.refuse(manifest_text(project("zephyr", revision="1234")), "zephyr", "1234")

    def test_text_that_is_not_yaml_is_refused(self):
        self.refuse("manifest: [unclosed\n", "cannot read", "west.yml")

    def test_a_manifest_without_projects_is_refused(self):
        self.refuse("manifest:\n  version: '1.2'\n", "projects")

    def test_an_empty_project_list_is_refused(self):
        self.refuse("manifest:\n  projects: []\n", "projects")

    def test_a_document_that_is_not_a_manifest_is_refused(self):
        self.refuse("- just\n- a list\n", "manifest")

    def test_a_project_without_a_name_is_refused(self):
        self.refuse(manifest_text(project("zephyr"), f"- revision: {SHA}"), "name")

    def test_a_duplicate_project_name_is_refused(self):
        self.refuse(manifest_text(project("zephyr"), project("zephyr", revision=OTHER)), "zephyr", "twice")

    def test_a_malformed_group_filter_entry_is_refused(self):
        self.refuse(manifest_text(project("zephyr"), group_filter="[crux-optional]"), "group-filter")

    def test_a_label_that_could_break_the_table_is_refused(self):
        for label in ("", "a|b", "a b", "a\nb", "`x`"):
            with self.subTest(label=label):
                self.refuse(manifest_text(project("zephyr")), "label", label=label)

    def test_a_missing_manifest_file_is_refused(self):
        status, _, err = run("--manifest", str(self.workspace.root / "absent.yml"), "--label", "v0")
        self.assertEqual(1, status)
        self.assertIn("absent.yml", err)
        self.assertNotIn("Traceback", err)

    def test_a_licence_class_that_is_not_a_plain_word_is_refused(self):
        extra = "userdata:\n  crux-licence-class: 'a | b'"
        self.refuse(manifest_text(project("zephyr", groups=["crux-optional"], extra=extra)),
                    "zephyr", "crux-licence-class")

    def test_the_label_is_required(self):
        status, _, err = run("--manifest", str(MANIFEST))
        self.assertEqual(2, status)
        self.assertIn("--label", err)


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class DeterminismTest(unittest.TestCase):
    def table(self, text):
        return load_module().build_table(text, "v0")

    def test_two_runs_give_the_same_bytes(self):
        first = run("--manifest", str(MANIFEST), "--label", "v0")
        second = run("--manifest", str(MANIFEST), "--label", "v0")
        self.assertEqual(first, second)

    def test_reordering_the_projects_changes_nothing(self):
        text = MANIFEST.read_text()
        reference = self.table(text)
        for seed in range(5):
            # Comments are part of the input, so reorder the text blocks, not the parsed data.
            blocks = split_projects(text)
            random.Random(seed).shuffle(blocks)
            self.assertEqual(reference, self.table(join_projects(text, blocks)), seed)

    def test_reordering_groups_and_group_filter_changes_nothing(self):
        a = manifest_text(project("zephyr"), project("m", groups=["g2", "g1"], comment="v1"),
                          group_filter="[-g1, -g2]")
        b = manifest_text(project("m", groups=["g1", "g2"], comment="v1"), project("zephyr"),
                          group_filter="[-g2, -g1]")
        self.assertEqual(self.table(a), self.table(b))

    def test_the_output_names_no_local_path(self):
        _, out, _ = run("--manifest", str(MANIFEST), "--label", "v0")
        self.assertNotIn(str(REPO), out)
        self.assertNotIn(str(FIXTURES), out)


def split_projects(text):
    """Split a manifest into the text blocks of its projects, for the reordering test."""
    lines = text.splitlines(keepends=True)
    start = next(i for i, line in enumerate(lines) if line.strip() == "projects:") + 1
    end = next(i for i, line in enumerate(lines) if line.startswith("  self:"))
    blocks, current = [], []
    for line in lines[start:end]:
        if line.startswith("    - name:") and current:
            blocks.append("".join(current))
            current = []
        current.append(line)
    blocks.append("".join(current))
    return blocks


def join_projects(text, blocks):
    lines = text.splitlines(keepends=True)
    start = next(i for i, line in enumerate(lines) if line.strip() == "projects:") + 1
    end = next(i for i, line in enumerate(lines) if line.startswith("  self:"))
    return "".join(lines[:start]) + "".join(blocks) + "".join(lines[end:])


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class OptionalGroupTest(unittest.TestCase):
    def table(self, *projects, group_filter="[-crux-optional]"):
        return load_module().build_table(manifest_text(*projects, group_filter=group_filter), "v0")

    def test_a_project_in_a_disabled_group_is_optional(self):
        out = self.table(project("zephyr"), project("wolfssl", groups=["crux-optional"]))
        self.assertIn("; optional: group `crux-optional`", out)

    def test_a_project_without_groups_is_never_optional(self):
        self.assertNotIn("optional:", self.table(project("zephyr")).splitlines()[2])

    def test_a_group_that_the_filter_does_not_disable_is_not_optional(self):
        out = self.table(project("zephyr"), project("m", groups=["crux-other"]))
        self.assertNotIn("optional:", out.splitlines()[2])

    def test_one_enabled_group_keeps_the_project_active(self):
        out = self.table(project("zephyr"), project("m", groups=["crux-optional", "crux-other"]))
        self.assertNotIn("optional:", out.splitlines()[2])

    def test_a_later_filter_entry_wins(self):
        out = self.table(project("zephyr"), project("m", groups=["g"]), group_filter="[-g, +g]")
        self.assertNotIn("optional:", out.splitlines()[2])
        out = self.table(project("zephyr"), project("m", groups=["g"]), group_filter="[+g, -g]")
        self.assertIn("optional: group `g`", out.splitlines()[2])

    def test_all_the_groups_of_an_optional_project_are_named_sorted(self):
        out = self.table(project("zephyr"), project("m", groups=["b", "a"]), group_filter="[-a, -b]")
        self.assertIn("optional: groups `a`, `b`", out.splitlines()[2])

    def test_a_manifest_without_a_group_filter_has_no_optional_project(self):
        text = "manifest:\n  projects:\n    - name: m\n      revision: " + SHA + "\n      groups: [g]\n"
        self.assertNotIn("optional", load_module().build_table(text, "v0"))

    def test_the_licence_class_appears_only_when_the_manifest_declares_it(self):
        declared = project("m", groups=["crux-optional"], extra="userdata:\n  crux-licence-class: class-x")
        out = self.table(project("zephyr"), declared, project("n", groups=["crux-optional"]))
        row = out.splitlines()[2]
        self.assertIn("; licence class class-x", row)
        self.assertEqual(1, row.count("licence class"))

    def test_a_declared_licence_class_on_an_active_project_is_still_shown(self):
        declared = project("m", extra="userdata:\n  crux-licence-class: class-x")
        self.assertIn("licence class class-x", self.table(project("zephyr"), declared))


@unittest.skipUnless(yaml, "PyYAML is required to parse the manifests; west installs it")
class CellTest(unittest.TestCase):
    def cells(self, text):
        out = load_module().build_table(text, "v0").splitlines()
        header = [c.strip() for c in out[0].strip("|").split("|")]
        row = [c.strip() for c in out[2].strip("|").split("|")]
        return dict(zip(header, row))

    def test_a_commit_is_abbreviated_to_twelve_characters_and_lowered(self):
        cells = self.cells(manifest_text(project("zephyr", revision=SHA.upper())))
        self.assertEqual(f"`{SHA[:12]}`", cells["zephyr"])

    def test_a_trailing_comment_that_is_a_version_is_shown(self):
        cells = self.cells(manifest_text(project("zephyr", comment="v4.4.2")))
        self.assertEqual(f"`{SHA[:12]}` (v4.4.2)", cells["zephyr"])

    def test_a_trailing_comment_that_is_not_a_version_is_not_shown(self):
        cells = self.cells(manifest_text(project("zephyr", comment="follows the Zephyr manifest")))
        self.assertEqual(f"`{SHA[:12]}`", cells["zephyr"])

    def test_words_and_dates_that_start_with_a_digit_are_not_versions(self):
        for comment in ("2026-11-08", "3-clause-BSD", "4ever", "1"):
            with self.subTest(comment=comment):
                cells = self.cells(manifest_text(project("zephyr", comment=comment)))
                self.assertEqual(f"`{SHA[:12]}`", cells["zephyr"])

    def test_with_two_revision_keys_the_commit_and_its_comment_are_the_last_pair(self):
        # yaml.safe_load, like west, keeps the last of two equal keys. The comment must come from it too.
        text = manifest_text(f"- name: zephyr\nrevision: {OTHER} # v1.0\nrevision: {SHA} # v2.0")
        self.assertEqual(f"`{SHA[:12]}` (v2.0)", self.cells(text)["zephyr"])

    def test_an_allowlist_given_as_one_string_is_read_as_one_name(self):
        text = manifest_text(project("zephyr", extra="import:\n  name-allowlist: hal_nxp"))
        self.assertEqual("imported from `zephyr`, not pinned here", self.cells(text)["hal_nxp"])

    def test_a_comment_after_a_quoted_revision_is_found(self):
        cells = self.cells(manifest_text(project("zephyr", revision=f'"{SHA}"', comment="v1.0")))
        self.assertEqual(f"`{SHA[:12]}` (v1.0)", cells["zephyr"])

    def test_a_comment_on_another_line_is_not_taken_for_the_version(self):
        text = manifest_text("- name: zephyr\n# v9.9.9\npath: zephyr\nrevision: " + SHA)
        self.assertEqual(f"`{SHA[:12]}`", self.cells(text)["zephyr"])

    def test_a_project_the_manifest_does_not_hold_is_said_to_be_absent(self):
        cells = self.cells(manifest_text(project("zephyr")))
        self.assertEqual("not in this manifest", cells["wolfssl"])
        self.assertEqual("not in this manifest", cells["hal_nxp"])

    def test_a_project_imported_by_allowlist_is_not_given_a_commit(self):
        text = manifest_text(project("zephyr", extra="import:\n  name-allowlist: [hal_nxp, cmsis]"))
        self.assertEqual("imported from `zephyr`, not pinned here", self.cells(text)["hal_nxp"])

    def test_a_pinned_project_wins_over_an_import_of_the_same_name(self):
        text = manifest_text(project("zephyr", extra="import:\n  name-allowlist: [hal_nxp]"),
                             project("hal_nxp", revision=OTHER))
        self.assertEqual(f"`{OTHER[:12]}`", self.cells(text)["hal_nxp"])

    def test_a_frozen_style_manifest_pins_the_hal_too(self):
        # west manifest --freeze writes every project with url and a full commit. Not run against west here.
        text = ("manifest:\n  projects:\n"
                f"    - name: zephyr\n      url: https://example.invalid/zephyr\n      revision: {SHA}\n"
                f"    - name: hal_nxp\n      url: https://example.invalid/hal\n      revision: {OTHER}\n"
                "  self:\n    path: crux-pilot\n")
        cells = self.cells(text)
        self.assertEqual(f"`{OTHER[:12]}`", cells["hal_nxp"])

    def test_an_import_without_an_allowlist_is_stated_not_guessed(self):
        out = load_module().build_table(manifest_text(project("zephyr", extra="import: true")), "v0")
        self.assertIn("`zephyr` imports further projects that this manifest does not pin.", out)
        self.assertNotIn("imported from", out)

    def test_extra_pinned_projects_get_a_column(self):
        cells = self.cells(manifest_text(project("zephyr"), project("zz-extra", revision=OTHER, comment="v2.1"),
                                         project("aa-extra", revision=SHA)))
        self.assertEqual(f"`{OTHER[:12]}` (v2.1)", cells["zz-extra"])
        header = list(cells)
        self.assertLess(header.index("aa-extra"), header.index("zz-extra"))


class NoPyYamlTest(unittest.TestCase):
    def test_a_missing_pyyaml_is_a_clear_message_not_a_traceback(self):
        module = load_module()
        err = io.StringIO()
        with mock.patch.dict(sys.modules, {"yaml": None}), contextlib.redirect_stderr(err):
            status = module.main(["--manifest", str(MANIFEST), "--label", "v0"])
        self.assertEqual(1, status)
        self.assertIn("PyYAML", err.getvalue())


class ScopeTest(unittest.TestCase):
    def test_the_script_imports_only_the_standard_library_and_pyyaml(self):
        tree = ast.parse(SCRIPT.read_text())
        found = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                found |= {alias.name.split(".")[0] for alias in node.names}
            elif isinstance(node, ast.ImportFrom):
                found.add(node.module.split(".")[0])
        self.assertLessEqual(found, {"argparse", "re", "sys", "pathlib", "yaml"})

    def test_the_script_uses_no_network_or_process_facility(self):
        source = SCRIPT.read_text()
        for word in ("socket", "urllib", "subprocess", "requests", "os.system"):
            self.assertNotIn(word, source, word)


class DocumentTest(unittest.TestCase):
    def test_the_document_shows_the_golden_table_so_that_it_cannot_drift(self):
        self.assertIn(EXPECTED.read_text(), GUIDE.read_text())

    def test_no_table_cell_of_the_document_holds_a_bare_pipe_in_a_code_span(self):
        for line in GUIDE.read_text().splitlines():
            if line.startswith("|"):
                self.assertNotIn("`|`", line)

    def test_the_document_says_that_no_tag_exists_and_has_a_diagram(self):
        text = GUIDE.read_text()
        self.assertIn("```mermaid", text)
        self.assertIn("No Crux tag exists", text)
        self.assertIn("confirmation", text)


if __name__ == "__main__":
    unittest.main()
