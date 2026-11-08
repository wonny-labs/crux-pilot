#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Print the Crux compatibility table for one revision of a west manifest. See docs/compatibility-table.md.

The table has one row, for the revision label given with --label, and one column for each project that the
manifest pins. Everything in it comes from the manifest text and from nothing else: no network, no git, no
Zephyr checkout. Each cell holds the pinned commit, shortened to 12 characters, and the version that the
manifest names in a trailing comment on the revision line, if it names one that looks like a version. The
version is the author's comment and is not checked against the commit.

A project in a group that the manifest's group-filter turns off is marked optional, and the group is named.
A licence class is named only when the project declares one in its manifest userdata, as
``crux-licence-class``. It is never read from prose.

A project that the manifest imports by name and does not pin, such as hal_nxp through Zephyr, gets no commit:
the cell says that it is imported. A frozen manifest, such as west manifest --freeze writes, pins it.

Every pinned project must name a full 40-character commit. A branch, a tag, a short commit, a missing
revision or a revision that is not a string is refused, with the project named. So is a manifest that cannot
be parsed. PyYAML is needed, as for west itself.

Exit status: 0 for a table, 1 for a refused manifest or label, 2 for a usage error.
"""

import argparse
import re
import sys
from pathlib import Path

DEFAULT_MANIFEST = Path(__file__).resolve().parents[1] / "west.yml"
# The projects the table always has a column for, in this order. The others that the manifest pins follow, sorted.
REQUIRED = ("zephyr", "hal_nxp", "mbedtls", "tf-psa-crypto", "wolfssl", "wolfssh", "wolfpsa", "reliance-edge")
SHORT = 12
LICENCE_KEY = "crux-licence-class"
FULL_COMMIT = re.compile(r"[0-9a-fA-F]{40}")
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*")
LABEL = re.compile(r"[A-Za-z0-9][A-Za-z0-9._@/+-]*")
VERSION = re.compile(r"v?[0-9][0-9A-Za-z._+-]*")


class ManifestError(Exception):
    """The manifest or the label cannot be turned into a table."""


def load_yaml():
    try:
        import yaml  # west depends on it. Imported late so that a missing module is a message.
    except ImportError:
        raise ManifestError("PyYAML is required to read the manifest. West installs it.")
    return yaml


def trailing_comment(node, lines):
    """Return the comment text after a scalar node on its own line, or None."""
    line = lines[node.end_mark.line] if node.end_mark.line < len(lines) else ""
    rest = line[node.end_mark.column:].strip()
    return rest[1:].strip() if rest.startswith("#") else None


def version_of(comment):
    return comment if comment is not None and VERSION.fullmatch(comment) else None


def project_nodes(yaml, text, source):
    """Return the YAML nodes of the manifest's project entries, in file order."""
    try:
        root = yaml.compose(text, Loader=yaml.SafeLoader)
        manifest = dict((key.value, value) for key, value in root.value)["manifest"]
        projects = dict((key.value, value) for key, value in manifest.value)["projects"]
    except (yaml.YAMLError, AttributeError, KeyError, TypeError, ValueError):
        raise ManifestError(f"cannot read the projects of {source}")
    return projects.value


def revision_node(node):
    for key, value in node.value:
        if key.value == "revision":
            return value
    return None


def read_manifest(text, source):
    """Return (projects, group_state, imports) from manifest text.

    projects maps a name to a dict with commit, version, groups and licence. group_state maps a group name
    to False for each group that the group-filter ends with turned off and True for each turned on.
    imports maps a project name to the sorted list of names it imports, or to None when it imports more
    than it names.
    """
    yaml = load_yaml()
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ManifestError(f"cannot read {source}: {error}")
    if not isinstance(data, dict) or not isinstance(data.get("manifest"), dict):
        raise ManifestError(f"cannot read {source}: there is no manifest: mapping")
    manifest = data["manifest"]
    entries = manifest.get("projects")
    if not isinstance(entries, list) or not entries:
        raise ManifestError(f"cannot read {source}: the manifest has no projects")
    nodes = project_nodes(yaml, text, source)
    if len(nodes) != len(entries):
        raise ManifestError(f"cannot read the projects of {source}: an alias or merge key hides a project")
    lines = text.splitlines()
    defaults = manifest.get("defaults")
    default_revision = defaults.get("revision") if isinstance(defaults, dict) else None

    group_state = {}
    group_filter = manifest.get("group-filter", [])
    if not isinstance(group_filter, list):
        raise ManifestError(f"{source}: group-filter must be a list")
    for item in group_filter:
        if not isinstance(item, str) or item[:1] not in "+-" or not NAME.fullmatch(item[1:]):
            raise ManifestError(f"{source}: group-filter entry {item!r} must be + or - and a group name")
        group_state[item[1:]] = item[0] == "+"

    projects, imports = {}, {}
    for entry, node in zip(entries, nodes):
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str) or not entry["name"]:
            raise ManifestError(f"{source}: a project has no name")
        name = entry["name"]
        if not NAME.fullmatch(name):
            raise ManifestError(f"{source}: the project name {name!r} has characters that a table cell cannot hold")
        if name in projects:
            raise ManifestError(f"{source}: the project {name} is listed twice")
        revision = entry.get("revision", default_revision)
        if revision is None:
            raise ManifestError(f"{name}: no revision. The table needs a full 40-character commit")
        if not isinstance(revision, str) or not FULL_COMMIT.fullmatch(revision):
            raise ManifestError(
                f"{name}: the revision {revision!r} is not a full 40-character commit. "
                "A branch, a tag, a short commit or an unquoted number is not a pin")
        groups = entry.get("groups", [])
        if not isinstance(groups, list) or not all(isinstance(g, str) and NAME.fullmatch(g) for g in groups):
            raise ManifestError(f"{name}: groups must be a list of group names")
        licence = None
        userdata = entry.get("userdata")
        if isinstance(userdata, dict) and LICENCE_KEY in userdata:
            licence = userdata[LICENCE_KEY]
            if not isinstance(licence, str) or not NAME.fullmatch(licence):
                raise ManifestError(f"{name}: {LICENCE_KEY} must be one plain word, got {licence!r}")
        value = revision_node(node)
        comment = trailing_comment(value, lines) if value is not None else None
        projects[name] = {"commit": revision.lower(), "version": version_of(comment),
                          "groups": sorted(groups), "licence": licence}
        spec = entry.get("import")
        if isinstance(spec, dict) and isinstance(spec.get("name-allowlist"), list):
            imports[name] = sorted(str(item) for item in spec["name-allowlist"])
        elif spec not in (None, False):
            imports[name] = None
    return projects, group_state, imports


def cell(name, projects, imports):
    if name in projects:
        info = projects[name]
        text = f"`{info['commit'][:SHORT]}`"
        if info["version"]:
            text += f" ({info['version']})"
        if info["optional"]:
            noun = "group" if len(info["groups"]) == 1 else "groups"
            text += "; optional: " + noun + " " + ", ".join(f"`{g}`" for g in info["groups"])
        if info["licence"]:
            text += f"; licence class {info['licence']}"
        return text
    importers = sorted(p for p, names in imports.items() if names is not None and name in names)
    if importers:
        return "imported from " + ", ".join(f"`{p}`" for p in importers) + ", not pinned here"
    return "not in this manifest"


def build_table(text, label, source="the manifest"):
    """Return the Markdown table for a manifest text and a revision label."""
    if not LABEL.fullmatch(label):
        raise ManifestError(f"the label {label!r} must be letters, digits and . _ @ / + - only")
    projects, group_state, imports = read_manifest(text, source)
    for info in projects.values():
        info["optional"] = bool(info["groups"]) and all(group_state.get(g, True) is False for g in info["groups"])
    columns = list(REQUIRED) + sorted(name for name in projects if name not in REQUIRED)
    rows = [
        "| Crux revision | " + " | ".join(columns) + " |",
        "| --- | " + " | ".join("---" for _ in columns) + " |",
        f"| {label} | " + " | ".join(cell(name, projects, imports) for name in columns) + " |",
    ]
    notes = [
        f"Commits are shortened to {SHORT} characters. A version in brackets is the trailing comment of the "
        "`revision:` line in the manifest. It is not checked against the commit.",
    ]
    off = sorted(group for group, enabled in group_state.items() if not enabled)
    if off:
        notes.append("Optional means inactive unless one of its groups is enabled. The manifest's "
                     "`group-filter` turns off: " + ", ".join(f"`{g}`" for g in off) + ".")
    for importer in sorted(imports):
        names = imports[importer]
        if names is None:
            notes.append(f"`{importer}` imports further projects that this manifest does not pin.")
            continue
        unpinned = [n for n in names if n not in projects]
        if unpinned:
            notes.append(f"`{importer}` imports without a pin here: " + ", ".join(f"`{n}`" for n in unpinned) + ".")
    return "\n".join(rows) + "\n\n" + "\n".join("- " + note for note in notes) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Print the Crux compatibility table for one manifest revision.")
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST), help="a west manifest (default: west.yml of this repository)")
    parser.add_argument("--label", required=True,
                        help="the Crux revision this row is for, such as a tag or main@<short sha>")
    args = parser.parse_args(argv)
    try:
        try:
            text = Path(args.manifest).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise ManifestError(f"cannot read {args.manifest}: {error}")
        table = build_table(text, args.label, args.manifest)
    except ManifestError as error:
        print(f"gen-compat-table: {error}", file=sys.stderr)
        return 1
    sys.stdout.write(table)
    return 0


if __name__ == "__main__":
    sys.exit(main())
