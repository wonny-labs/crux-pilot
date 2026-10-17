#!/usr/bin/env python3
"""Record source, toolchain, configuration, test, and firmware identity for a Zephyr build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import stat
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET


REPO = Path(__file__).resolve().parents[1]
SOURCE_HASH_ALGORITHM = "sha256-path-mode-content-v1"


def run(command: list[str], cwd: Path, *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=check)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, display_path: str) -> dict[str, Any]:
    resolved = path.resolve(strict=True)
    record: dict[str, Any] = {"path": display_path, "size_bytes": path.stat().st_size}
    if path.is_symlink():
        record["type"] = "symlink"
        record["sha256"] = hashlib.sha256(os.readlink(path).encode()).hexdigest()
    else:
        record["type"] = "file"
        record["sha256"] = sha256_file(resolved)
    return record


def source_snapshot(repo: Path) -> dict[str, Any]:
    listed = run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], repo
    ).stdout
    paths = sorted(
        {
            os.fsdecode(raw)
            for raw in listed.encode().split(b"\0")
            if raw and os.fsdecode(raw) != ".DS_Store"
        }
    )
    digest = hashlib.sha256()
    for relative in paths:
        path = repo / relative
        if not path.exists() and not path.is_symlink():
            continue
        resolved = path.resolve(strict=True)
        resolved.relative_to(repo)
        mode = stat.S_IMODE(path.lstat().st_mode)
        if path.is_symlink():
            kind, content = b"symlink", os.readlink(path).encode()
        elif path.is_file():
            kind, content = b"file", path.read_bytes()
        else:
            continue
        digest.update(relative.encode() + b"\0" + kind + b"\0")
        digest.update(f"{mode:o}".encode() + b"\0" + hashlib.sha256(content).digest())

    status = run(["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"], repo).stdout
    records = [record for record in status.encode().split(b"\0") if record]
    counts = {"modified_or_staged": 0, "untracked": 0, "deleted": 0, "renamed_or_copied": 0}
    index = 0
    while index < len(records):
        record = records[index]
        code = record[:2].decode("ascii", errors="replace")
        if code == "??":
            counts["untracked"] += 1
        elif "D" in code:
            counts["deleted"] += 1
        elif "R" in code or "C" in code:
            counts["renamed_or_copied"] += 1
            index += 1
        else:
            counts["modified_or_staged"] += 1
        index += 1

    head = run(["git", "rev-parse", "--verify", "HEAD"], repo, check=False)
    branch = run(["git", "branch", "--show-current"], repo, check=False)
    return {
        "git_commit": head.stdout.strip() if head.returncode == 0 else None,
        "git_branch": branch.stdout.strip() or None,
        "dirty": any(counts.values()),
        "git_status_counts": counts,
        "snapshot_algorithm": SOURCE_HASH_ALGORITHM,
        "snapshot_file_count": len(paths),
        "snapshot_sha256": digest.hexdigest(),
        "west_manifest_sha256": sha256_file(repo / "west.yml"),
    }


def cmake_cache(build_dir: Path) -> dict[str, str]:
    cache: dict[str, str] = {}
    cache_file = build_dir / "CMakeCache.txt"
    for line in cache_file.read_text().splitlines():
        match = re.match(r"([^:#]+):[^=]+=([^=]*)$", line)
        if match:
            cache[match.group(1)] = match.group(2)
    return cache


def command_version(command: str | None, arguments: list[str]) -> str | None:
    if not command:
        return None
    executable = shutil.which(command) or (command if Path(command).is_file() else None)
    if not executable:
        return None
    result = run([executable, *arguments], Path.cwd(), check=False)
    if result.returncode:
        return None
    return next((line.strip() for line in result.stdout.splitlines() if line.strip()), None)


def image_identity(repo: Path) -> dict[str, Any]:
    dockerfile = repo / ".devcontainer/Dockerfile"
    devcontainer = repo / ".devcontainer/devcontainer.json"
    from_line = next(
        (line.split(None, 1)[1] for line in dockerfile.read_text().splitlines()
         if line.strip().startswith("FROM ")),
        None,
    )
    packages = command_version("dpkg-query", ["--version"])
    package_manifest_hash = None
    if packages:
        package_list = run(
            ["dpkg-query", "-W", "-f=${binary:Package}=${Version}\\n"], repo, check=False
        )
        if package_list.returncode == 0:
            package_manifest_hash = hashlib.sha256(
                "\n".join(sorted(package_list.stdout.splitlines())).encode()
            ).hexdigest()
    in_container = Path("/.dockerenv").exists()
    github_runner = {
        "provider": "github-actions" if os.environ.get("GITHUB_ACTIONS") == "true" else None,
        "os": os.environ.get("ImageOS"),
        "image_version": os.environ.get("ImageVersion"),
        "run_id": os.environ.get("GITHUB_RUN_ID"),
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
        "event": os.environ.get("GITHUB_EVENT_NAME"),
        "ref": os.environ.get("GITHUB_REF"),
        "workflow": os.environ.get("GITHUB_WORKFLOW"),
    }
    return {
        "execution_environment": {
            "kind": "github-actions-runner"
            if github_runner["provider"]
            else "container"
            if in_container
            else "host",
            "runtime_image_id": os.environ.get("CRUX_BUILD_IMAGE_ID") if in_container else None,
            "runner_image": {
                "os": github_runner["os"],
                "version": github_runner["image_version"],
            }
            if github_runner["provider"]
            else None,
        },
        "devcontainer_recipe": {
            "base_image": from_line,
            "dockerfile_sha256": sha256_file(dockerfile),
            "config_sha256": sha256_file(devcontainer),
        }
        if in_container
        else None,
        "installed_package_inventory_sha256": package_manifest_hash,
        "github_runner": github_runner,
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--workspace", type=Path, default=Path(os.environ.get("CRUX_WORKSPACE", REPO.parent)))
    arguments = parser.parse_args()

    repo = REPO.resolve()
    build_dir = arguments.build_dir.resolve()
    build_dir.relative_to(repo)
    workspace = arguments.workspace.resolve()
    build_root = build_dir / "zephyr"
    workspace_config = workspace / ".west/config"
    if not workspace_config.is_file():
        parser.error(f"west workspace configuration is missing: {workspace_config}")

    required_artifacts = [
        build_root / "zephyr.bin",
        build_root / "zephyr.elf",
        build_root / ".config",
        build_root / "zephyr.dts",
        build_root / "zephyr.stat",
        build_root / "zephyr.map",
        build_dir / "rom.json",
        build_dir / "ram.json",
    ]
    missing = [str(path.relative_to(repo)) for path in required_artifacts if not path.is_file()]
    if missing:
        parser.error("build is missing required provenance inputs: " + ", ".join(missing))

    cache = cmake_cache(build_dir)
    compiler = cache.get("CMAKE_C_COMPILER")
    tools = {
        "cmake": command_version("cmake", ["--version"]),
        "ninja": command_version("ninja", ["--version"]),
        "west": command_version("west", ["--version"]),
        "c_compiler": {
            "path": compiler,
            "version": command_version(compiler, ["--version"]),
            "id": cache.get("CMAKE_C_COMPILER_ID"),
        },
        "zephyr_sdk_install_dir": os.environ.get("ZEPHYR_SDK_INSTALL_DIR"),
        "zephyr_toolchain_variant": cache.get("ZEPHYR_TOOLCHAIN_VARIANT"),
        "python_executable": cache.get("Python3_EXECUTABLE"),
    }
    projects = []
    project_list = run(
        ["west", "list", "--format={name}\t{revision}\t{path}"], workspace
    ).stdout
    for line in project_list.splitlines():
        name, revision, path = line.split("\t", 2)
        projects.append({"name": name, "revision": revision, "path": path})

    artifacts = [file_record(path, path.relative_to(repo).as_posix()) for path in required_artifacts]
    test_root = repo / "build/zephyr/twister-time"
    test_files = []
    if test_root.is_dir():
        test_names = {
            "twister.json",
            "twister.xml",
            "twister_report.xml",
            "twister_suite_report.xml",
            "handler.log",
            "build.log",
        }
        for path in sorted(test_root.rglob("*")):
            if path.is_file() and path.name in test_names:
                test_files.append(file_record(path, path.relative_to(repo).as_posix()))

    test_summary = None
    report = test_root / "twister_report.xml"
    if report.is_file():
        root = ET.parse(report).getroot()
        suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
        test_summary = {
            key: sum(int(suite.attrib.get(key, 0)) for suite in suites)
            for key in ("tests", "failures", "errors", "skipped")
        }

    for relative in (
        "build/evidence/evkb-build.log",
        "build/evidence/zephyr-rom-report.log",
        "build/evidence/zephyr-ram-report.log",
    ):
        path = repo / relative
        if path.is_file():
            artifacts.append(file_record(path, relative))

    config_hashes = {
        path.name: sha256_file(path)
        for path in (build_root / ".config", build_root / "zephyr.dts")
    }
    source = source_snapshot(repo)
    image = image_identity(repo)
    stable_image = {
        **image,
        "github_runner": {
            "provider": image["github_runner"]["provider"],
            "os": image["github_runner"]["os"],
            "image_version": image["github_runner"]["image_version"],
        },
    }
    identity = {
        "source": source,
        "target": arguments.target,
        "configuration_sha256": config_hashes,
        "west_projects": projects,
        "west_workspace_config_sha256": sha256_file(workspace_config),
        "tools": tools,
        "image": stable_image,
        "build_outputs": artifacts[: len(required_artifacts)],
    }
    build_id = hashlib.sha256(
        json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    manifest = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "build_id": build_id,
        "repository": repo.name,
        "target": arguments.target,
        "build_directory": build_dir.relative_to(repo).as_posix(),
        "source": source,
        "tools": tools,
        "image": image,
        "ci_run": {
            "repository": os.environ.get("GITHUB_REPOSITORY"),
            "server_url": os.environ.get("GITHUB_SERVER_URL"),
            "run_url": (
                f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}"
                f"/actions/runs/{os.environ['GITHUB_RUN_ID']}"
                if all(os.environ.get(key) for key in
                       ("GITHUB_SERVER_URL", "GITHUB_REPOSITORY", "GITHUB_RUN_ID"))
                else None
            ),
        },
        "west_projects": projects,
        "west_workspace_config_sha256": sha256_file(workspace_config),
        "test_summary": test_summary,
        "test_artifacts": test_files,
        "artifacts": artifacts,
    }
    output = build_dir / "provenance.json"
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"Build provenance: {output}")
    print(f"Build identity:   {build_id}")
    print(f"Source snapshot:  {source['snapshot_sha256']} ({source['snapshot_file_count']} files)")
    print(f"Dirty source:     {source['dirty']}")
    print(f"Test summary:     {test_summary or 'no Twister report found'}")
    print(f"Artifacts hashed: {len(artifacts)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
