#!/usr/bin/env python3
"""Validate and compare the three complete Logging EVKB image builds.

ROM/Flash and RAM values are the root totals from Zephyr's standard
``rom_report`` and ``ram_report`` outputs. They describe whole images, not an
isolated Logging-backend cost.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
BOARD = "mimxrt1060_evk@B//qspi"
PROFILES = ("uart", "udp-only", "uart-udp")
EXPECTED_BACKENDS = {
    "uart": (True, False),
    "udp-only": (False, True),
    "uart-udp": (True, True),
}
REQUIRED_ARTIFACTS = (
    "zephyr/zephyr.bin",
    "zephyr/zephyr.elf",
    "zephyr/.config",
    "zephyr/zephyr.dts",
    "zephyr/zephyr.stat",
    "zephyr/zephyr.map",
    "rom.json",
    "ram.json",
)
TOOL_KEYS = (
    "cmake",
    "ninja",
    "west",
    "c_compiler",
    "zephyr_sdk_install_dir",
    "zephyr_toolchain_variant",
    "python_executable",
)
RUNNER_KEYS = (
    "provider",
    "os",
    "image_version",
    "run_id",
    "run_attempt",
    "event",
    "ref",
    "workflow",
)
FIELDS = (
    "profile",
    "board",
    "application",
    "source_commit",
    "source_snapshot_sha256",
    "build_id",
    "rom_flash_root_bytes",
    "ram_root_bytes",
    "flash_delta_vs_uart_bytes",
    "ram_delta_vs_uart_bytes",
    "zephyr_bin_bytes",
    "zephyr_elf_bytes",
    "config_sha256",
    "rom_report_sha256",
    "ram_report_sha256",
    "provenance_sha256",
)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read JSON evidence {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"JSON evidence must be an object: {path}")
    return value


def report_total(path: Path) -> int:
    report = read_json(path)
    total = report.get("total_size")
    root = report.get("symbols")
    if type(total) is not int or total < 0:
        raise ValueError(f"invalid whole-image report total in {path}")
    if isinstance(root, dict) and root.get("size") != total:
        raise ValueError(f"report root and total_size disagree in {path}")
    return total


def config_values(path: Path) -> dict[str, str | bool]:
    values: dict[str, str | bool] = {}
    for line in path.read_text().splitlines():
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2:-11]] = False
        elif line.startswith("CONFIG_") and "=" in line:
            name, value = line.split("=", 1)
            values[name] = value
    return values


def config_enabled(values: dict[str, str | bool], name: str, profile: str) -> bool:
    value = values.get(name)
    if value is False:
        return False
    if value == "y":
        return True
    raise ValueError(f"{profile}: resolved Kconfig does not define {name}")


def runner_identity(image: dict[str, Any]) -> dict[str, Any]:
    runner = image.get("github_runner")
    if not isinstance(runner, dict):
        raise ValueError("build provenance is missing GitHub runner identity")
    return {key: runner.get(key) for key in RUNNER_KEYS}


def common_identity(manifest: dict[str, Any]) -> dict[str, Any]:
    source = manifest.get("source")
    tools = manifest.get("tools")
    image = manifest.get("image")
    if not isinstance(source, dict) or not isinstance(tools, dict) or not isinstance(image, dict):
        raise ValueError("build provenance is missing source, tool, or runner identity")
    if source.get("dirty") or not source.get("recoverable_from_git"):
        raise ValueError("image provenance must identify a clean, committed source tree")
    return {
        "target": manifest.get("target"),
        "source": source,
        "west_projects": manifest.get("west_projects"),
        "west_workspace_config_sha256": manifest.get("west_workspace_config_sha256"),
        "tools": {key: tools.get(key) for key in TOOL_KEYS},
        "execution_environment": image.get("execution_environment"),
        "github_runner": runner_identity(image),
    }


def load_profile(repo: Path, profile: str) -> dict[str, Any]:
    build = repo / "build/zephyr" / f"logging-evkb-{profile}"
    manifest_path = build / "provenance.json"
    manifest = read_json(manifest_path)
    if manifest.get("target") != BOARD:
        raise ValueError(f"{profile}: expected target {BOARD!r}")

    expected_application = (repo / "tests/logging_consumer/zephyr").resolve()
    cache = build / "CMakeCache.txt"
    home = next(
        (line.split("=", 1)[1] for line in cache.read_text().splitlines()
         if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")),
        None,
    )
    if home is None or Path(home).resolve() != expected_application:
        raise ValueError(f"{profile}: build is not the shared Logging consumer application")

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list):
        raise ValueError(f"{profile}: provenance has no artifact hash list")
    by_path = {
        item.get("path"): item
        for item in artifacts
        if isinstance(item, dict) and isinstance(item.get("path"), str)
    }
    for relative in REQUIRED_ARTIFACTS:
        repo_relative = (build / relative).relative_to(repo).as_posix()
        path = repo / repo_relative
        record = by_path.get(repo_relative)
        if not path.is_file() or not isinstance(record, dict):
            raise ValueError(f"{profile}: missing required build artifact {repo_relative}")
        if record.get("sha256") != sha256_file(path):
            raise ValueError(f"{profile}: artifact hash mismatch for {repo_relative}")

    config_path = build / "zephyr/.config"
    values = config_values(config_path)
    expected_uart, expected_net = EXPECTED_BACKENDS[profile]
    if (config_enabled(values, "CONFIG_LOG_BACKEND_UART", profile) != expected_uart or
            config_enabled(values, "CONFIG_LOG_BACKEND_NET", profile) != expected_net):
        raise ValueError(f"{profile}: resolved Kconfig backend selection is incorrect")
    if expected_net:
        if values.get("CONFIG_LOG_BACKEND_NET_OUTPUT_TEXT") != "y":
            raise ValueError(f"{profile}: NET backend must resolve to text output")
        if values.get("CONFIG_LOG_BACKEND_NET_MAX_BUF_SIZE") != "480":
            raise ValueError(f"{profile}: NET backend buffer must resolve to 480 bytes")

    config_hash = sha256_file(config_path)
    if manifest.get("configuration_sha256", {}).get(".config") != config_hash:
        raise ValueError(f"{profile}: resolved Kconfig hash differs from provenance")
    rom_path = build / "rom.json"
    ram_path = build / "ram.json"
    return {
        "profile": profile,
        "board": BOARD,
        "application": "tests/logging_consumer/zephyr",
        "source_commit": manifest["source"]["git_commit"],
        "source_snapshot_sha256": manifest["source"]["snapshot_sha256"],
        "build_id": manifest.get("build_id"),
        "rom_flash_root_bytes": report_total(rom_path),
        "ram_root_bytes": report_total(ram_path),
        "zephyr_bin_bytes": (build / "zephyr/zephyr.bin").stat().st_size,
        "zephyr_elf_bytes": (build / "zephyr/zephyr.elf").stat().st_size,
        "config_sha256": config_hash,
        "rom_report_sha256": sha256_file(rom_path),
        "ram_report_sha256": sha256_file(ram_path),
        "provenance_sha256": sha256_file(manifest_path),
        "_common_identity": common_identity(manifest),
    }


def build_comparison(repo: Path = REPO) -> list[dict[str, Any]]:
    profiles = [load_profile(repo.resolve(), profile) for profile in PROFILES]
    identity = profiles[0]["_common_identity"]
    for profile in profiles[1:]:
        if profile["_common_identity"] != identity:
            raise ValueError(
                "source identity differs or toolchain/build environment differs across profiles"
            )
    if len({profile["source_commit"] for profile in profiles}) != 1:
        raise ValueError("image source identity differs across profiles")
    if len({profile["source_snapshot_sha256"] for profile in profiles}) != 1:
        raise ValueError("image source identity differs across profiles")
    if len({profile["build_id"] for profile in profiles}) != len(PROFILES):
        raise ValueError("build IDs must be unique for the three resolved configurations")
    if len({profile["config_sha256"] for profile in profiles}) != len(PROFILES):
        raise ValueError("resolved Kconfig must differ across the three selected profiles")

    flash_base = profiles[0]["rom_flash_root_bytes"]
    ram_base = profiles[0]["ram_root_bytes"]
    for profile in profiles:
        profile["flash_delta_vs_uart_bytes"] = profile["rom_flash_root_bytes"] - flash_base
        profile["ram_delta_vs_uart_bytes"] = profile["ram_root_bytes"] - ram_base
        del profile["_common_identity"]
    return profiles


def write_comparison(rows: list[dict[str, Any]], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=REPO)
    parser.add_argument(
        "--output", type=Path,
        default=REPO / "build/evidence/logging-evkb/resource-comparison.tsv",
    )
    arguments = parser.parse_args()
    rows = build_comparison(arguments.repo.resolve())
    write_comparison(rows, arguments.output)
    print(f"Comparable whole-image ROM/Flash and RAM evidence: {arguments.output}")
    for row in rows:
        print(
            f"{row['profile']}: ROM/Flash {row['rom_flash_root_bytes']} B, "
            f"RAM {row['ram_root_bytes']} B; build {row['build_id']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
