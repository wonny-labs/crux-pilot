#!/usr/bin/env python3
"""Measure which repository-owned C and C++ sources clang-tidy covers.

Read-only. The script compares four things and changes nothing:

* the sources the Linux host builds compile (one or more compile_commands.json files),
* the sources the repository's scripts pass to clang-tidy, shell globs expanded,
* which of those scripts a GitHub workflow reaches, directly or through another script,
* the C and C++ files that exist in the tree.

It prints a Markdown summary and can write the full result as JSON and the list of compiled
but not analysed sources for a report-only clang-tidy run.

Run `measure-tidy-coverage.py --self-test` for its unit tests.
"""

from __future__ import annotations

import argparse
import collections
import glob
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

OWNED_ROOTS = ("components", "samples", "tests", "web")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx")
HEADER_SUFFIXES = (".h", ".hh", ".hpp")
HEADER_FILTER = re.compile(r".*(components|samples|tests)/.*")
# The primary database is the one the CI analysis step passes with `$build_dir`.
PRIMARY = "host-debug"
UNRESOLVED_DIRECTORY_VARIABLES = {"$build_dir": PRIMARY}

# Reasons a tree source is in no host compile database. Ordered, first match wins. Path based, so
# every row is INFERRED from names and from the CMake and script references named in the report.
NOT_COMPILED_RULES = (
    ("zephyr-or-native_sim-only", re.compile(
        r"(^|/)(ports/zephyr|zephyr|zephyr_[a-z_]+|[a-z0-9_]+_zephyr|evkb|native_sim)(/|$)"
        r"|^tests/(native|zephyr)/|^tests/support/emmc_sim/"
        r"|^samples/(storage_[a-z_]+|flash_safety_poc|product_template)/"
        r"|^tests/(controller_integrated|controller_m3|controller_network|controller_network_stack|m5_integrated"
        r"|networking_physical|networking_physical_native|gpio_physical|messaging_resources|psa_entropy)/"
        r"|^tests/external_application/main\.c$|^tests/execution/src/native\.c$"
        r"|^tests/networking_consumer/main\.c$|^tests/ssh/cli/(compile|openssh)/"
        r"|^tests/ssh/sftp/native_sim/|^components/ssh/sftp/port/|^samples/time/src/(main|time_shell)\.c$")),
    ("psa-crypto-composition", re.compile(
        r"(^|/)ports/psa/|^tests/psa_(crypto|ports)/|^tests/tls_certificate/psa/|^tests/ssh/sftp/openssh/")),
    ("boundary-probe-or-consumer-fixture", re.compile(
        r"^tests/[a-z_]+_consumer/|^tests/consumer_(boundary|poc)/|^tests/external_application/")),
    ("orphan-no-build-reference", re.compile(r"^tests/queue/red_stub\.c$")),
)


def is_source(path: str) -> bool:
    return path.endswith(SOURCE_SUFFIXES)


def is_header(path: str) -> bool:
    return path.endswith(HEADER_SUFFIXES)


def owned(rel: str) -> bool:
    return rel.split("/", 1)[0] in OWNED_ROOTS


def relative_to(root: str, path: str) -> str | None:
    """Return path relative to root, or None when it lies outside or in the build directory."""
    root = os.path.normpath(root)
    path = os.path.normpath(path)
    if path != root and not path.startswith(root + os.sep):
        return None
    rel = os.path.relpath(path, root).replace(os.sep, "/")
    return None if rel == "build" or rel.startswith("build/") else rel


def load_database(path: str, db_root: str, repo_root: str) -> dict[str, dict]:
    """Return {repo-relative owned source: compile entry} for one compile_commands.json."""
    result: dict[str, dict] = {}
    for entry in json.loads(Path(path).read_text()):
        absolute = os.path.join(entry["directory"], entry["file"])
        rel = relative_to(db_root, absolute)
        if rel is not None and owned(rel):
            result[rel] = entry
    return result


def directory_name_for(directory: str, databases: dict[str, str]) -> str | None:
    """Map a repo-relative build directory to a database name (build/linux/debug is host-debug)."""
    if directory in UNRESOLVED_DIRECTORY_VARIABLES:
        return UNRESOLVED_DIRECTORY_VARIABLES[directory]
    if directory == "build/linux/debug":
        return PRIMARY
    if directory == "build/linux/sanitize":
        return "host-sanitize"
    name = directory.rsplit("/", 1)[-1] if directory.startswith("build/") else None
    return name if name in databases else None


def _logical_lines(text: str) -> list[tuple[int, str]]:
    lines, buffer, start = [], "", 0
    for number, line in enumerate(text.splitlines(), 1):
        if not buffer:
            start = number
        if line.rstrip().endswith("\\"):
            buffer += line.rstrip()[:-1] + " "
            continue
        lines.append((start, buffer + line))
        buffer = ""
    if buffer:
        lines.append((start, buffer))
    return lines


def _clean_variable(token: str) -> str:
    token = token.replace("${repo}", "").replace("$repo", "")
    return token.lstrip("/")


def tidy_invocations(script: str) -> list[dict]:
    """Find clang-tidy invocations: the database directory and the file patterns of each."""
    found = []
    start = re.compile(r"""(?:^|[\s;&|(])["']?\$\{?(?:tidy|CLANG_TIDY)\}?["']?\s""")
    for number, line in _logical_lines(Path(script).read_text()):
        match = start.search(line)
        if not match or line.lstrip().startswith("#"):
            continue
        rest = re.split(r"\s(?:\|\||&&|;)\s|\s(?:\|\||&&)$|\|\|", line[match.end():])[0]
        try:
            tokens = shlex.split(rest)
        except ValueError:
            continue
        directory, patterns, unresolved, index = None, [], [], 0
        while index < len(tokens):
            token = tokens[index]
            if token == "-p" and index + 1 < len(tokens):
                directory = _clean_variable(tokens[index + 1]) if tokens[index + 1] not in UNRESOLVED_DIRECTORY_VARIABLES \
                    else tokens[index + 1]
                index += 2
                continue
            if token.startswith("-"):
                index += 1
                continue
            cleaned = _clean_variable(token)
            (unresolved if "$" in cleaned else patterns).append(cleaned if "$" not in cleaned else token)
            index += 1
        found.append({"script": script, "line": number, "directory": directory,
                      "patterns": patterns, "unresolved": unresolved})
    return found


def expand(repo: str, patterns: list[str]) -> list[str]:
    files = set()
    for pattern in patterns:
        for hit in glob.glob(os.path.join(repo, pattern)):
            if os.path.isfile(hit) and is_source(hit):
                files.add(os.path.relpath(hit, repo).replace(os.sep, "/"))
    return sorted(files)


def workflow_reachable_scripts(repo: str) -> dict[str, str]:
    """Scripts a workflow references, directly or through other scripts. Value is the path of reference."""
    reference = re.compile(r"[\w./-]+\.(?:sh|py)")

    def referenced(text: str) -> set[str]:
        names = set()
        for token in reference.findall(text):
            token = token.lstrip("./")
            for candidate in (token, "scripts/" + os.path.basename(token)):
                if os.path.isfile(os.path.join(repo, candidate)):
                    names.add(candidate)
                    break
        return names

    reached: dict[str, str] = {}
    queue: list[tuple[str, str]] = []
    for workflow in sorted(glob.glob(os.path.join(repo, ".github", "workflows", "*.y*ml"))):
        label = os.path.relpath(workflow, repo)
        for name in sorted(referenced(Path(workflow).read_text())):
            if name not in reached:
                reached[name] = label
                queue.append((name, label))
    while queue:
        name, label = queue.pop()
        try:
            text = Path(repo, name).read_text()
        except (OSError, UnicodeDecodeError):
            continue
        for child in sorted(referenced(text)):
            if child not in reached:
                reached[child] = label + " -> " + name
                queue.append((child, reached[child]))
    return reached


def tree_sources(repo: str) -> list[str]:
    try:
        listing = subprocess.run(["git", "-C", repo, "ls-files", *OWNED_ROOTS], check=True,
                                 capture_output=True, text=True).stdout.splitlines()
        if listing:
            return sorted(path for path in listing if is_source(path) or is_header(path))
    except (OSError, subprocess.CalledProcessError):
        pass
    found = []
    for root in OWNED_ROOTS:
        for base, directories, names in os.walk(os.path.join(repo, root)):
            directories[:] = [d for d in directories if d not in ("build", "node_modules", ".git")]
            for name in names:
                if is_source(name) or is_header(name):
                    found.append(os.path.relpath(os.path.join(base, name), repo).replace(os.sep, "/"))
    return sorted(found)


def classify_not_compiled(rel: str) -> str:
    for reason, pattern in NOT_COMPILED_RULES:
        if pattern.search(rel):
            return reason
    return "unclassified"


def component_of(rel: str) -> str:
    parts = rel.split("/")
    return "/".join(parts[:2]) if len(parts) > 2 else parts[0]


def headers_of(entry: dict, db_root: str) -> set[str]:
    """Owned headers a translation unit includes (clang -MM). Needs the compiler of the database."""
    command = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    cleaned, skip = [], False
    for token in command:
        if skip:
            skip = False
        elif token in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif token not in ("-c", "-MD", "-MMD"):
            cleaned.append(token)
    try:
        output = subprocess.run(cleaned + ["-MM"], cwd=entry["directory"], capture_output=True, text=True)
    except OSError:
        return set()  # the build directory or the compiler is gone
    if output.returncode != 0:
        return set()
    names = output.stdout.replace("\\\n", " ").split(":", 1)[-1].split()
    result = set()
    for name in names:
        rel = relative_to(db_root, os.path.normpath(os.path.join(entry["directory"], name)))
        if rel is not None and owned(rel) and is_header(rel):
            result.add(rel)
    return result


def measure(repo: str, databases: dict[str, str], db_root: str, with_headers: bool,
            not_run: frozenset[str] = frozenset()) -> dict:
    tree = tree_sources(repo)
    tree_src = [path for path in tree if is_source(path)]
    tree_hdr = [path for path in tree if is_header(path)]
    entries = {name: load_database(path, db_root, repo) for name, path in databases.items()}
    primary = set(entries.get(PRIMARY, {}))
    other = set().union(*(set(v) for n, v in entries.items() if n != PRIMARY)) if len(entries) > 1 else set()
    compiled_any = primary | other

    # A script a workflow reaches may call clang-tidy only in a mode the workflow does not select;
    # the caller names those after reading the script, so they do not count as run.
    reached = {k: v for k, v in workflow_reachable_scripts(repo).items() if k not in not_run}
    invocations = []
    for script in sorted(glob.glob(os.path.join(repo, "scripts", "*.sh")) + glob.glob(os.path.join(repo, "tests", "**", "*.sh"), recursive=True)):
        invocations += tidy_invocations(script)
    analysed: dict[str, dict] = {}
    per_script: dict[str, list[str]] = collections.defaultdict(list)
    missing_from_database = []
    unresolved_directories = []
    for item in invocations:
        script_rel = os.path.relpath(item["script"], repo).replace(os.sep, "/")
        files = expand(repo, item["patterns"])
        database = directory_name_for(item["directory"] or "", databases)
        item.update(script=script_rel, files=files, database=database, in_ci=script_rel in reached)
        for rel in files:
            slot = analysed.setdefault(rel, {"scripts": set(), "ci": False})
            slot["scripts"].add(script_rel)
            slot["ci"] = slot["ci"] or item["in_ci"]
            per_script[script_rel].append(rel)
            if database in entries and rel not in entries[database]:
                missing_from_database.append((script_rel, database, rel))
        if database not in entries:
            # Membership in the build's database cannot be checked for this invocation.
            unresolved_directories.append([script_rel, item["directory"], len(files)])

    def analysed_by(scripts_filter) -> set[str]:
        return {rel for rel, slot in analysed.items() if any(scripts_filter(s) for s in slot["scripts"])}

    analyze_host = analysed_by(lambda s: s == "scripts/analyze-host.sh")
    ci_analysed = analysed_by(lambda s: s in reached)
    all_analysed = set(analysed)

    result: dict = {
        "tree_sources": len(tree_src),
        "tree_headers": len(tree_hdr),
        "database_sizes": {name: len(value) for name, value in entries.items()},
        "primary_compiled": len(primary),
        "compiled_in_any_host_database": len(compiled_any),
        "compiled_not_in_tree": sorted(compiled_any - set(tree_src)),
        "analysed_by_analyze_host_script": len(analyze_host),
        "analysed_by_workflow_reached_scripts": len(ci_analysed),
        "analysed_by_any_script": len(all_analysed),
        "tidy_scripts": {},
        "analysed_not_in_database": [list(item) for item in missing_from_database],
        "analysed_with_unresolved_build_directory": unresolved_directories,
        "analysed_not_in_tree": sorted(all_analysed - set(tree_src)),
    }
    for item in invocations:
        entry = result["tidy_scripts"].setdefault(item["script"], {"in_workflow": item["in_ci"],
                                                                 "reached_via": reached.get(item["script"]),
                                                                 "files": 0, "unresolved": []})
        entry["files"] += len(item["files"])
        entry["unresolved"] += item["unresolved"]

    for label, analysed_set in (("analyze_host", analyze_host), ("workflow", ci_analysed), ("any_script", all_analysed)):
        result[f"primary_not_analysed_{label}"] = sorted(primary - analysed_set)
        result[f"compiled_any_not_analysed_{label}"] = sorted(compiled_any - analysed_set)

    not_compiled = [path for path in tree_src if path not in compiled_any]
    reasons: dict[str, list[str]] = collections.defaultdict(list)
    for path in not_compiled:
        reasons[classify_not_compiled(path)].append(path)
    result["not_in_any_host_database"] = {key: sorted(value) for key, value in sorted(reasons.items())}
    result["only_in_other_host_databases"] = sorted(other - primary)

    by_directory: dict[str, dict[str, int]] = collections.defaultdict(lambda: collections.Counter())
    for path in sorted(primary):
        by_directory[component_of(path)]["compiled"] += 1
        by_directory[component_of(path)]["analysed_workflow" if path in ci_analysed else "gap"] += 1
    result["primary_by_component"] = {key: dict(value) for key, value in sorted(by_directory.items())}

    if with_headers:
        compiled_headers: set[str] = set()
        analysed_headers: set[str] = set()
        for name, table in entries.items():
            for rel, entry in table.items():
                found = headers_of(entry, db_root)
                compiled_headers |= found
                if rel in ci_analysed:
                    analysed_headers |= found
        in_filter = {h for h in tree_hdr if HEADER_FILTER.match("/" + h)}
        result["headers"] = {
            "in_tree": len(tree_hdr), "match_header_filter": len(in_filter),
            "included_by_a_compiled_source": len(compiled_headers),
            "included_by_a_workflow_analysed_source": len(analysed_headers & in_filter),
            "never_included_by_a_compiled_source": sorted(set(tree_hdr) - compiled_headers),
            "reached_only_through_unanalysed_sources": sorted((compiled_headers - analysed_headers) & in_filter),
        }
    return result


def table(rows: list[tuple], header: tuple) -> str:
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    out += ["| " + " | ".join(str(cell) for cell in row) + " |" for row in rows]
    return "\n".join(out)


def render(result: dict) -> str:
    lines = ["# clang-tidy coverage", ""]
    lines.append(table([
        ("C/C++ sources in the tree (components, samples, tests, web)", result["tree_sources"]),
        ("Sources compiled by the primary host build (" + PRIMARY + ")", result["primary_compiled"]),
        ("Sources compiled by any host build given", result["compiled_in_any_host_database"]),
        ("Primary sources clang-tidy sees through scripts/analyze-host.sh",
         result["primary_compiled"] - len(result["primary_not_analysed_analyze_host"])),
        ("Primary sources clang-tidy sees through any script a workflow reaches",
         result["primary_compiled"] - len(result["primary_not_analysed_workflow"])),
        ("Primary sources clang-tidy sees through any script",
         result["primary_compiled"] - len(result["primary_not_analysed_any_script"])),
        ("Primary sources never passed to clang-tidy by a workflow-reached script",
         len(result["primary_not_analysed_workflow"])),
    ], ("Measure", "Count")))
    lines += ["", "## Scripts that run clang-tidy", ""]
    lines.append(table([(name, "yes" if value["in_workflow"] else "no", value["reached_via"] or "", value["files"])
                        for name, value in sorted(result["tidy_scripts"].items())],
                       ("Script", "Reached by a workflow", "Reached via", "Files after glob expansion")))
    lines += ["", "## Primary build by component", ""]
    lines.append(table([(name, v.get("compiled", 0), v.get("analysed_workflow", 0), v.get("gap", 0))
                        for name, v in result["primary_by_component"].items()],
                       ("Component", "Compiled", "Analysed (workflow-reached)", "Not analysed")))
    lines += ["", "## Tree sources in no host compile database", ""]
    lines.append(table([(key, len(value)) for key, value in result["not_in_any_host_database"].items()],
                       ("Reason (inferred from path)", "Sources")))
    for key in ("analysed_not_in_database", "analysed_not_in_tree", "compiled_not_in_tree"):
        lines += ["", f"{key}: {len(result[key])} (checked only for build directories that match a database given)"]
    lines += ["", "Invocations whose build directory matches no database given (membership not checked):"]
    lines += [f"- {script}: `-p {directory}`, {count} files" for script, directory, count in
              result["analysed_with_unresolved_build_directory"]]
    if "headers" in result:
        header = result["headers"]
        lines += ["", "## Headers", ""]
        lines.append(table([(key, value if not isinstance(value, list) else len(value)) for key, value in header.items()],
                           ("Measure", "Count")))
    return "\n".join(lines) + "\n"


class SelfTest(unittest.TestCase):
    def make_repo(self, root: str) -> None:
        files = {
            "components/a/src/a.c": "int a;\n", "components/a/src/b.c": "int b;\n",
            "components/a/ports/zephyr/z.c": "int z;\n", "tests/a/t.c": "int t;\n",
            "tests/native/n/main.c": "int n;\n", "tests/x_consumer/probe.c": "int p;\n",
            "scripts/run-a.sh": 'tidy="${CLANG_TIDY:-clang-tidy-18}"\n'
                                '"$tidy" -p "$repo/build/linux/debug" \\\n    "$repo"/components/a/src/a.c \\\n'
                                '    "$repo"/tests/a/*.c\n',
            "scripts/orphan.sh": '"$tidy" -p "$repo/build/linux/debug" "$repo"/components/a/src/*.c || fail x\n',
            ".github/workflows/ci.yml": "steps:\n  - run: ./scripts/run-a.sh\n",
        }
        for name, text in files.items():
            Path(root, name).parent.mkdir(parents=True, exist_ok=True)
            Path(root, name).write_text(text)
        entries = [{"directory": root + "/build/linux/debug", "file": root + "/" + f, "command": "cc -c x"}
                   for f in ("components/a/src/a.c", "components/a/src/b.c", "tests/a/t.c")]
        entries.append({"directory": root, "file": "/usr/include/stdio.h", "command": "cc"})
        entries.append({"directory": root, "file": "build/generated.c", "command": "cc"})
        Path(root, "build/linux/debug").mkdir(parents=True)
        Path(root, "build/linux/debug/compile_commands.json").write_text(json.dumps(entries))

    def test_measure(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.make_repo(root)
            db = {PRIMARY: os.path.join(root, "build/linux/debug/compile_commands.json")}
            result = measure(root, db, root, with_headers=False)
            self.assertEqual(result["tree_sources"], 6)
            self.assertEqual(result["primary_compiled"], 3)
            self.assertEqual(result["primary_not_analysed_workflow"], ["components/a/src/b.c"])
            self.assertEqual(result["primary_not_analysed_any_script"], [])
            self.assertEqual(result["primary_not_analysed_analyze_host"], ["components/a/src/a.c", "components/a/src/b.c", "tests/a/t.c"])
            self.assertFalse(result["tidy_scripts"]["scripts/orphan.sh"]["in_workflow"])
            self.assertTrue(result["tidy_scripts"]["scripts/run-a.sh"]["in_workflow"])
            self.assertEqual(sorted(result["not_in_any_host_database"]),
                             ["boundary-probe-or-consumer-fixture", "zephyr-or-native_sim-only"])
            self.assertEqual(result["analysed_not_in_database"], [])
            self.assertEqual(result["analysed_with_unresolved_build_directory"], [])

    def test_not_run_mode(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.make_repo(root)
            db = {PRIMARY: os.path.join(root, "build/linux/debug/compile_commands.json")}
            result = measure(root, db, root, with_headers=False, not_run=frozenset({"scripts/run-a.sh"}))
            self.assertFalse(result["tidy_scripts"]["scripts/run-a.sh"]["in_workflow"])
            self.assertEqual(len(result["primary_not_analysed_workflow"]), 3)

    def test_analysed_but_not_compiled(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.make_repo(root)
            Path(root, "components/a/src/c.c").write_text("int c;\n")
            db = {PRIMARY: os.path.join(root, "build/linux/debug/compile_commands.json")}
            result = measure(root, db, root, with_headers=False)
            self.assertIn(["scripts/orphan.sh", PRIMARY, "components/a/src/c.c"], result["analysed_not_in_database"])

    def test_unresolved_directory_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.make_repo(root)
            Path(root, "scripts/other.sh").write_text('"$tidy" -p "$build" --quiet "$repo"/tests/a/t.c\n')
            db = {PRIMARY: os.path.join(root, "build/linux/debug/compile_commands.json")}
            result = measure(root, db, root, with_headers=False)
            self.assertEqual(result["analysed_with_unresolved_build_directory"], [["scripts/other.sh", "$build", 1]])

    def test_classify(self) -> None:
        self.assertEqual(classify_not_compiled("components/time/ports/zephyr/x.c"), "zephyr-or-native_sim-only")
        self.assertEqual(classify_not_compiled("tests/web_consumer/rejected/a.c"), "boundary-probe-or-consumer-fixture")
        self.assertEqual(classify_not_compiled("components/new/src/new.c"), "unclassified")

    def test_invocation_parsing(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "s.sh")
            Path(path).write_text('x=1\n"$tidy" -p "$repo/build/logging-consumer" --quiet \\\n  "$repo/tests/a.c" "$other"/b.c || fail\n')
            (item,) = tidy_invocations(path)
            self.assertEqual(item["directory"], "build/logging-consumer")
            self.assertEqual(item["patterns"], ["tests/a.c"])
            self.assertEqual(item["unresolved"], ["$other/b.c"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default=".", help="repository root (default: current directory)")
    parser.add_argument("--db", action="append", default=[], metavar="NAME=PATH",
                        help="a compile_commands.json; name host-debug is the primary build")
    parser.add_argument("--db-root", help="the checkout path recorded inside the databases (default: --repo)")
    parser.add_argument("--json", help="write the full result as JSON")
    parser.add_argument("--gap-list", help="write the primary-build sources no workflow-reached script analyses")
    parser.add_argument("--not-run", action="append", default=[], metavar="SCRIPT",
                        help="a workflow-reached script whose clang-tidy step no workflow mode selects (repeatable)")
    parser.add_argument("--headers", action="store_true", help="also compute headers reached (runs clang -MM)")
    parser.add_argument("--self-test", action="store_true", help="run the unit tests and exit")
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(SelfTest)).wasSuccessful() else 1
    databases = dict(item.split("=", 1) for item in args.db)
    if PRIMARY not in databases:
        parser.error("a database named host-debug is required")
    repo = os.path.abspath(args.repo)
    result = measure(repo, databases, os.path.abspath(args.db_root or repo), args.headers,
                     frozenset(args.not_run))
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=1, sort_keys=True) + "\n")
    if args.gap_list:
        Path(args.gap_list).write_text("\n".join(result["primary_not_analysed_workflow"]) + "\n")
    sys.stdout.write(render(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
