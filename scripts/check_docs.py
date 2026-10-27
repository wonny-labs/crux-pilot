#!/usr/bin/env python3
"""Check maintained Markdown links without parsing Markdown in project code."""

import argparse
import hashlib
import io
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY_PATH = ROOT / "doc-links.toml"
LYCHEE_CONFIG = ROOT / "lychee.toml"

# Official release archive names, not compiler or source-build requirements.
ARCHIVES = {
    ("Linux", "x86_64"): ("x86_64-unknown-linux-musl", "linux_x86_64_sha256"),
    ("Darwin", "arm64"): ("aarch64-apple-darwin", "macos_arm64_sha256"),
}


def load_policy(path=POLICY_PATH):
    with path.open("rb") as stream:
        policy = tomllib.load(stream)
    if policy.get("schema") != 1:
        raise ValueError("Unsupported doc-links.toml schema")
    return policy


def tracked_markdown(root):
    raw = subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "-z", "--", "*.md"]
    )
    paths = [os.fsdecode(entry) for entry in raw.split(b"\0") if entry]
    # The working tree can contain unstaged deletions. Other maintained documents
    # pointing to a deleted file will still produce a link failure.
    return sorted(path for path in paths if (root / path).is_file())


def _rules(selection, kind):
    entries = selection.get(kind, [])
    if not isinstance(entries, list):
        raise ValueError(f"selection.{kind} must be a list")
    rules = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError(f"selection.{kind} must contain tables")
        path = entry.get("path")
        reason = entry.get("reason")
        if not isinstance(path, str) or not isinstance(reason, str) or not reason.strip():
            raise ValueError(f"selection.{kind} needs a path and nonempty reason")
        if (not path or path.startswith("/") or "\\" in path or
                any(part in ("", ".", "..") for part in path.rstrip("/").split("/"))):
            raise ValueError(f"Invalid selection path: {path!r}")
        if not path.endswith("/") and not path.endswith(".md"):
            raise ValueError(f"Selection files must be Markdown: {path!r}")
        rules.append((path, reason))
    if len(set(path for path, _ in rules)) != len(rules):
        raise ValueError(f"Duplicate selection.{kind} path")
    return rules


def _matches(rule_path, path):
    return path.startswith(rule_path) if rule_path.endswith("/") else path == rule_path


def select_paths(paths, selection):
    includes = _rules(selection, "include")
    excludes = _rules(selection, "exclude")
    tracked = sorted(set(paths))
    for kind, rules in (("include", includes), ("exclude", excludes)):
        for rule_path, _ in rules:
            if not any(_matches(rule_path, path) for path in tracked):
                raise ValueError(f"selection.{kind} matches no tracked Markdown: {rule_path}")

    included, excluded = [], {}
    for path in tracked:
        if any(_matches(rule, path) for rule, _ in includes):
            included.append(path)
        else:
            matched = next(((rule, reason) for rule, reason in excludes
                            if _matches(rule, path)), None)
            if matched is None:
                included.append(path)
            else:
                excluded[path] = matched[1]

    if not included:
        raise ValueError("No maintained Markdown files selected")
    return included, excluded


def tool_info(policy, root=ROOT):
    entry = policy["tool"]
    version = entry["version"]
    if not isinstance(version, str) or re.fullmatch(r"\d+\.\d+\.\d+", version) is None:
        raise ValueError("Tool version must be pinned to a numeric release")
    machine = platform.machine().lower()
    if platform.system() == "Darwin" and machine == "aarch64":
        machine = "arm64"
    target = ARCHIVES.get((platform.system(), machine))
    if target is None:
        raise ValueError(f"Unsupported lychee tool host: {platform.system()}/{machine}")
    archive, checksum_key = target
    digest = entry[checksum_key]
    if not isinstance(digest, str) or re.fullmatch("[0-9a-f]{64}", digest) is None:
        raise ValueError(f"Missing SHA-256 for {checksum_key}")
    cache_root = Path(os.environ.get("CRUX_CACHE_DIR", str(root.parent / ".crux-cache")))
    binary = cache_root.expanduser().resolve() / "tools" / f"lychee-{version}" / "lychee"
    url = (f"https://github.com/lycheeverse/lychee/releases/download/"
           f"lychee-v{version}/lychee-{archive}.tar.gz")
    return binary, version, digest, url


def verified_version(binary, version):
    if not binary.is_file():
        return False
    result = subprocess.run(
        [str(binary), "--version"], capture_output=True, text=True, check=False
    )
    return result.returncode == 0 and re.search(
        rf"(?<!\d){re.escape(version)}(?!\d)", result.stdout
    ) is not None


def install_tool(policy, root=ROOT):
    binary, version, digest, url = tool_info(policy, root)
    if verified_version(binary, version):
        print(f"lychee {version} already installed: {binary}")
        return binary

    print(f"Downloading pinned lychee {version} from the official release")
    with urllib.request.urlopen(url, timeout=120) as response:
        archive_bytes = response.read()
    if hashlib.sha256(archive_bytes).hexdigest() != digest:
        raise ValueError("lychee release archive SHA-256 mismatch")

    with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode="r:gz") as archive:
        members = [member for member in archive.getmembers()
                   if member.isfile() and Path(member.name).name == "lychee"]
        if len(members) != 1:
            raise ValueError("Expected exactly one lychee executable in release archive")
        stream = archive.extractfile(members[0])
        if stream is None:
            raise ValueError("Could not read lychee executable from archive")
        binary.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(dir=binary.parent, delete=False) as temp:
                temp_name = temp.name
                shutil.copyfileobj(stream, temp)
            os.chmod(temp_name, 0o755)
            if not verified_version(Path(temp_name), version):
                raise ValueError("Downloaded lychee executable reports the wrong version")
            os.replace(temp_name, binary)
            temp_name = None
        finally:
            if temp_name is not None:
                Path(temp_name).unlink(missing_ok=True)
    print(f"Installed lychee {version}: {binary}")
    return binary


def run_lychee(root, binary, selected):
    # --files-from trims each line when deciding whether it is a comment.
    # Refuse ambiguous paths instead of silently skipping a maintained document.
    if any(
        "\n" in path
        or "\r" in path
        or path != path.strip()
        or path.lstrip().startswith("#")
        for path in selected
    ):
        raise ValueError("Markdown filename cannot be represented in lychee input list")
    # Keep original paths so relative links resolve relative to each source file.
    inputs = "\n".join(selected) + "\n"
    result = subprocess.run(
        [str(binary), "--config", str(root / "lychee.toml"),
         "--root-dir", str(root), "--files-from", "-"],
        input=inputs, text=True, cwd=root, check=False,
    )
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--install", action="store_true", help="Install pinned lychee")
    actions.add_argument("--list", action="store_true", help="Show inspected/excluded Markdown")
    args = parser.parse_args()

    try:
        policy = load_policy()
        if args.install:
            install_tool(policy)
            return 0

        selected, excluded = select_paths(
            tracked_markdown(ROOT), policy.get("selection", {})
        )
        print(f"Markdown selection: {len(selected)} maintained, {len(excluded)} archived")
        if args.list:
            for name in selected:
                print(f"CHECK   {name}")
            for name, reason in sorted(excluded.items()):
                print(f"ARCHIVE {name} — {reason}")
            return 0

        binary, version, _, _ = tool_info(policy)
        if not verified_version(binary, version):
            raise ValueError("Pinned lychee is missing or mismatched. Run --install first.")
        return run_lychee(ROOT, binary, selected)
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError,
            tarfile.TarError) as error:
        print(f"Document link check setup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
