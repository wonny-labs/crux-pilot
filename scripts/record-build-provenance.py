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
FIRMWARE_IDENTITY_ALGORITHM = "crux-firmware-identity-v1"
DISABLED_OPTION = re.compile(rb"# CONFIG_[A-Za-z0-9_]+ is not set")


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
    file_count = 0
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
        file_count += 1
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
        "recoverable_from_git": head.returncode == 0 and not any(counts.values()),
        "git_status_counts": counts,
        "snapshot_algorithm": SOURCE_HASH_ALGORITHM,
        "snapshot_file_count": file_count,
        "snapshot_sha256": digest.hexdigest(),
        "west_manifest_sha256": sha256_file(repo / "west.yml"),
    }


def workspace_inputs(repo: Path, workspace: Path) -> dict[str, Any]:
    projects = []
    listing = run(["west", "list", "--format={name}\t{revision}\t{path}"], workspace).stdout
    for line in listing.splitlines():
        name, revision, relative = line.split("\t", 2)
        project = (workspace / relative).resolve()
        if project == repo:
            continue
        head = run(["git", "rev-parse", "HEAD"], project).stdout.strip()
        expected = run(["git", "rev-parse", f"{revision}^{{commit}}"], project).stdout.strip()
        # Finder metadata is not a compiler input. Do not delete it from user checkouts.
        dirty = run(["git", "status", "--porcelain=v1", "--untracked-files=all", "--",
                     ".", ":(exclude)**/.DS_Store", ":(exclude).DS_Store"], project).stdout.strip()
        if head != expected or dirty:
            raise ValueError(f"Dependency {name} must match its pinned revision and be clean: "
                             f"HEAD={head}, expected={expected}, status={dirty!r}")
        projects.append({"name": name, "revision": revision, "path": relative,
                         "git_commit": head, "dirty": False})
    return {"source": source_snapshot(repo), "west_projects": projects,
            "west_workspace_config_sha256": sha256_file(workspace / ".west/config")}


def require_matching_inputs(expected: dict[str, Any], actual: dict[str, Any]) -> None:
    if expected != actual:
        raise ValueError("Source or workspace inputs changed during verification; rerun the checks")


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


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


def normalised_config(content: bytes) -> bytes:
    """Keep what Kconfig decided. Comments can name workspace paths; blank lines mean nothing."""
    kept = [line for line in content.splitlines()
            if line and (not line.startswith(b"#") or DISABLED_OPTION.fullmatch(line))]
    return b"".join(line + b"\n" for line in kept)


def firmware_identity(build_root: Path, target: str) -> dict[str, str]:
    """Identify the firmware by what the build produced, not by where it was built."""
    config = normalised_config((build_root / ".config").read_bytes())
    return {
        "algorithm": FIRMWARE_IDENTITY_ALGORITHM,
        "target": target,
        "image_sha256": sha256_file(build_root / "zephyr.bin"),
        "configuration_sha256": hashlib.sha256(config).hexdigest(),
    }


def firmware_digest(identity: dict[str, str]) -> str:
    return hashlib.sha256(
        json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def in_container() -> bool:
    return Path("/.dockerenv").exists()


def missing_default_recipe(repo: Path) -> list[str]:
    """Name what a build of Crux itself must have. The Dockerfile is required on any host."""
    required = [".devcontainer/Dockerfile"]
    if in_container():
        required.append(".devcontainer/devcontainer.json")
    return [name for name in required if not (repo / name).is_file()]


def find_devcontainer(repo: Path, explicit: Path | None) -> tuple[Path | None, str | None]:
    if explicit:
        return explicit.resolve(), "explicit"
    if (repo / ".devcontainer").is_dir():
        return repo / ".devcontainer", "repository"
    if (REPO / ".devcontainer").is_dir():
        return REPO / ".devcontainer", "crux"
    return None, None


def recipe_source(repo: Path, directory: Path | None, origin: str | None) -> dict[str, str | None]:
    """Say which recipe was used without recording where it lives outside the repository."""
    if directory is None:
        return {"directory": None, "origin": None}
    try:
        display = directory.relative_to(repo).as_posix()
    except ValueError:
        display = directory.name
    return {"directory": display, "origin": origin}


def devcontainer_recipe(directory: Path | None) -> dict[str, str | None] | None:
    """Hash the recipe files that exist. An image-based recipe has no Dockerfile."""
    if directory is None:
        return None
    dockerfile = directory / "Dockerfile"
    config = directory / "devcontainer.json"
    if not dockerfile.is_file() and not config.is_file():
        return None
    from_line = None
    if dockerfile.is_file():
        from_line = next(
            (line.split(None, 1)[1] for line in dockerfile.read_text().splitlines()
             if line.strip().startswith("FROM ")),
            None,
        )
    return {
        "base_image": from_line,
        "dockerfile_sha256": sha256_file(dockerfile) if dockerfile.is_file() else None,
        "config_sha256": sha256_file(config) if config.is_file() else None,
    }


def image_identity(repo: Path, recipe_directory: Path | None) -> dict[str, Any]:
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
    container = in_container()
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
            if container
            else "host",
            "runtime_image_id": os.environ.get("CRUX_BUILD_IMAGE_ID") if container else None,
            "runner_image": {
                "os": github_runner["os"],
                "version": github_runner["image_version"],
            }
            if github_runner["provider"]
            else None,
        },
        "devcontainer_recipe": devcontainer_recipe(recipe_directory) if container else None,
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
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--target")
    parser.add_argument("--capture-inputs", type=Path)
    parser.add_argument("--inputs", type=Path)
    parser.add_argument("--test-inputs", type=Path)
    parser.add_argument("--workspace", type=Path,
                        help="west workspace; default $CRUX_WORKSPACE, otherwise the repository's parent")
    parser.add_argument("--repo", type=Path,
                        help="repository that was built, such as a product that consumes Crux as a "
                             "west project; default the Crux checkout that holds this script")
    parser.add_argument("--devcontainer", type=Path, metavar="DIR",
                        help="container recipe directory; default <repo>/.devcontainer when present, "
                             "otherwise Crux's own")
    parser.add_argument("--test-root", type=Path,
                        help="Twister output inside the repository; "
                             "default <repo>/build/zephyr/twister-time")
    arguments = parser.parse_args()

    repo = (arguments.repo or REPO).resolve()
    if arguments.repo and not (repo / "west.yml").is_file():
        parser.error(f"repository has no west manifest: {repo / 'west.yml'}")
    workspace = (arguments.workspace or Path(os.environ.get("CRUX_WORKSPACE", repo.parent))).resolve()
    workspace_config = workspace / ".west/config"
    if not workspace_config.is_file():
        parser.error(f"west workspace configuration is missing: {workspace_config}")

    if arguments.capture_inputs:
        write_json(arguments.capture_inputs, workspace_inputs(repo, workspace))
        print(f"Verification inputs: {arguments.capture_inputs}")
        return 0
    if not arguments.build_dir or not arguments.target or not arguments.inputs:
        parser.error("recording requires --build-dir, --target and pre-build --inputs")
    build_dir = arguments.build_dir.resolve()
    if not build_dir.is_relative_to(repo):
        parser.error(f"build directory {build_dir} is outside the repository {repo}")
    test_root = repo / "build/zephyr/twister-time"
    if arguments.test_root:
        test_root = arguments.test_root.resolve()
        if not test_root.is_relative_to(repo):
            parser.error(f"test root {test_root} is outside the repository {repo}")
    recipe_directory, recipe_origin = find_devcontainer(repo, arguments.devcontainer)
    if arguments.devcontainer and not devcontainer_recipe(recipe_directory):
        parser.error(f"--devcontainer holds no Dockerfile or devcontainer.json: {recipe_directory}")
    output = build_dir / "provenance.json"
    output.unlink(missing_ok=True)
    if not arguments.repo and not arguments.devcontainer:
        missing = missing_default_recipe(repo)
        if missing:
            parser.error("container recipe is missing: " + ", ".join(missing))
    build_root = build_dir / "zephyr"
    captured_inputs = json.loads(arguments.inputs.read_text())
    current_inputs = workspace_inputs(repo, workspace)
    require_matching_inputs(captured_inputs, current_inputs)
    if arguments.test_inputs:
        require_matching_inputs(json.loads(arguments.test_inputs.read_text()), current_inputs)

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
    projects = current_inputs["west_projects"]

    artifacts = [file_record(path, path.relative_to(repo).as_posix()) for path in required_artifacts]
    test_files = []
    if arguments.test_inputs and test_root.is_dir():
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
    if arguments.test_inputs and report.is_file():
        root = ET.parse(report).getroot()
        suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
        test_summary = {
            key: sum(int(suite.attrib.get(key, 0)) for suite in suites)
            for key in ("tests", "failures", "errors", "skipped")
        }

    if arguments.test_inputs and (not test_summary or test_summary["tests"] == 0
                                 or any(test_summary[key] for key in ("failures", "errors", "skipped"))):
        raise ValueError("Matching test inputs require a complete passing Twister report")

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
    require_matching_inputs(captured_inputs, workspace_inputs(repo, workspace))
    source = current_inputs["source"]
    image = image_identity(repo, recipe_directory)
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
        "west_workspace_config_sha256": current_inputs["west_workspace_config_sha256"],
        "tools": tools,
        "image": stable_image,
        "build_outputs": artifacts[: len(required_artifacts)],
    }
    build_id = hashlib.sha256(
        json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    firmware = firmware_identity(build_root, arguments.target)
    manifest = {
        "schema_version": 2,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "build_id": build_id,
        # Outside `identity` on purpose: build_id keeps its inputs. Do not move these into `image`.
        "firmware_identity": firmware,
        "devcontainer_recipe_source": recipe_source(repo, recipe_directory, recipe_origin)
        if in_container()
        else None,
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
        "west_workspace_config_sha256": current_inputs["west_workspace_config_sha256"],
        "test_inputs_sha256": sha256_file(arguments.test_inputs) if arguments.test_inputs else None,
        "test_summary": test_summary,
        "test_artifacts": test_files,
        "artifacts": artifacts,
    }
    write_json(output, manifest)
    print(f"Build provenance: {output}")
    print(f"Build identity:   {build_id}")
    print(f"Firmware identity: {firmware_digest(firmware)}")
    print(f"Source snapshot:  {source['snapshot_sha256']} ({source['snapshot_file_count']} files)")
    print(f"Source recoverable from Git: {source['recoverable_from_git']}")
    print(f"Test summary:     {test_summary or 'no Twister report found'}")
    print(f"Artifacts hashed: {len(artifacts)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
