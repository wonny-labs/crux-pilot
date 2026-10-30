"""Regression coverage for comparable, retained Logging EVKB build evidence."""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts/compare-logging-evkb-resources.py"
SPEC = importlib.util.spec_from_file_location("logging_evkb_resources", SCRIPT)
RESOURCES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESOURCES)


PROFILES = ("uart", "udp-only", "uart-udp")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_profile(root: Path, profile: str, rom_size: int, ram_size: int,
                  *, commit: str = "a" * 40, uart: bool | None = None,
                  net: bool | None = None) -> None:
    build = root / "build/zephyr" / f"logging-evkb-{profile}"
    zephyr = build / "zephyr"
    zephyr.mkdir(parents=True, exist_ok=True)
    if uart is None:
        uart = profile != "udp-only"
    if net is None:
        net = profile != "uart"
    config_lines = [
        "CONFIG_LOG_BACKEND_UART=y" if uart else "# CONFIG_LOG_BACKEND_UART is not set",
        "CONFIG_LOG_BACKEND_NET=y" if net else "# CONFIG_LOG_BACKEND_NET is not set",
    ]
    if net:
        config_lines.extend([
            "CONFIG_LOG_BACKEND_NET_OUTPUT_TEXT=y",
            "CONFIG_LOG_BACKEND_NET_MAX_BUF_SIZE=480",
        ])
    artifacts = {}
    for relative, contents in {
        f"build/zephyr/logging-evkb-{profile}/zephyr/zephyr.bin": b"image-bin",
        f"build/zephyr/logging-evkb-{profile}/zephyr/zephyr.elf": b"image-elf",
        f"build/zephyr/logging-evkb-{profile}/zephyr/.config": ("\n".join(config_lines) + "\n").encode(),
        f"build/zephyr/logging-evkb-{profile}/zephyr/zephyr.dts": b"/dts-v1/;\n",
        f"build/zephyr/logging-evkb-{profile}/zephyr/zephyr.stat": b"elf-stat",
        f"build/zephyr/logging-evkb-{profile}/zephyr/zephyr.map": b"linker-map",
        f"build/zephyr/logging-evkb-{profile}/rom.json": json.dumps({
            "total_size": rom_size, "symbols": {"name": "Root", "size": rom_size}
        }).encode(),
        f"build/zephyr/logging-evkb-{profile}/ram.json": json.dumps({
            "total_size": ram_size, "symbols": {"name": "Root", "size": ram_size}
        }).encode(),
    }.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        artifacts[relative] = {"path": relative, "sha256": sha256(path), "size_bytes": path.stat().st_size}

    app = root / "tests/logging_consumer/zephyr"
    app.mkdir(parents=True, exist_ok=True)
    (build / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={app}\n")
    manifest = {
        "target": "mimxrt1060_evk@B//qspi",
        "build_id": f"build-{profile}",
        "build_directory": f"build/zephyr/logging-evkb-{profile}",
        "source": {
            "git_commit": commit,
            "git_branch": "feat/issue-110-configurable-uart-udp",
            "snapshot_sha256": "b" * 64,
            "dirty": False,
            "recoverable_from_git": True,
        },
        "west_projects": [{"name": "zephyr", "git_commit": "c" * 40, "dirty": False}],
        "west_workspace_config_sha256": "d" * 64,
        "configuration_sha256": {".config": artifacts[
            f"build/zephyr/logging-evkb-{profile}/zephyr/.config"]["sha256"]},
        "tools": {
            "cmake": "cmake version 3.28.4",
            "ninja": "1.11.1",
            "west": "West version: v1.4.0",
            "c_compiler": {"id": "GNU", "version": "gcc 14.3.0"},
            "zephyr_sdk_install_dir": "/opt/zephyr-sdk-1.0.1",
            "zephyr_toolchain_variant": "zephyr",
            "python_executable": "/usr/bin/python3",
        },
        "image": {
            "execution_environment": {
                "kind": "github-actions-runner",
                "runner_image": {"os": "ubuntu24", "version": "2026.10"},
            },
            "github_runner": {
                "run_id": "12345",
                "run_attempt": "1",
                "event": "workflow_dispatch",
                "ref": "refs/heads/feat/issue-110-configurable-uart-udp",
                "workflow": "zephyr-software",
                "os": "Linux",
                "image_version": "2026.10",
            },
        },
        "artifacts": list(artifacts.values()),
    }
    (build / "provenance.json").write_text(json.dumps(manifest))


class LoggingEvkbEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for profile, rom, ram in (("uart", 100, 50), ("udp-only", 200, 80), ("uart-udp", 220, 85)):
            write_profile(self.root, profile, rom, ram)

    def test_comparison_uses_same_clean_source_toolchain_and_consumer(self):
        rows = RESOURCES.build_comparison(self.root)
        self.assertEqual([row["profile"] for row in rows], list(PROFILES))
        self.assertEqual([row["rom_flash_root_bytes"] for row in rows], [100, 200, 220])
        self.assertEqual([row["ram_root_bytes"] for row in rows], [50, 80, 85])
        self.assertEqual([row["flash_delta_vs_uart_bytes"] for row in rows], [0, 100, 120])
        self.assertEqual([row["ram_delta_vs_uart_bytes"] for row in rows], [0, 30, 35])

    def test_tsv_retains_build_identity_and_comparable_sizes(self):
        rows = RESOURCES.build_comparison(self.root)
        output = self.root / "resource-comparison.tsv"
        RESOURCES.write_comparison(rows, output)
        with output.open(newline="") as stream:
            data = list(csv.DictReader(stream, delimiter="\t"))
        self.assertEqual(data[1]["build_id"], "build-udp-only")
        self.assertEqual(data[1]["rom_flash_root_bytes"], "200")
        self.assertEqual(data[1]["ram_root_bytes"], "80")
        self.assertEqual(len(data[1]["provenance_sha256"]), 64)

    def test_mismatched_source_commit_is_rejected(self):
        build = self.root / "build/zephyr/logging-evkb-uart-udp/provenance.json"
        manifest = json.loads(build.read_text())
        manifest["source"]["git_commit"] = "e" * 40
        build.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "source identity differs"):
            RESOURCES.build_comparison(self.root)

    def test_disabled_backend_must_match_each_profile(self):
        for profile, rom, ram in (("uart", 100, 50), ("udp-only", 200, 80), ("uart-udp", 220, 85)):
            write_profile(self.root, profile, rom, ram, uart=True, net=True)
        with self.assertRaisesRegex(ValueError, "backend selection"):
            RESOURCES.build_comparison(self.root)

    def test_report_file_hash_must_match_its_provenance_manifest(self):
        report = self.root / "build/zephyr/logging-evkb-udp-only/rom.json"
        report.write_text('{"total_size": 999}\n')
        with self.assertRaisesRegex(ValueError, "artifact hash mismatch"):
            RESOURCES.build_comparison(self.root)

    def test_workflow_retains_all_three_profile_build_evidence(self):
        workflow = (REPO / ".github/workflows/zephyr.yml").read_text()
        for evidence_path in (
            "crux-pilot/build/zephyr/logging-evkb-*/provenance.json",
            "crux-pilot/build/zephyr/logging-evkb-*/rom.json",
            "crux-pilot/build/zephyr/logging-evkb-*/ram.json",
            "crux-pilot/build/zephyr/logging-evkb-*/CMakeCache.txt",
            "crux-pilot/build/zephyr/logging-evkb-*/compile_commands.json",
            "crux-pilot/build/zephyr/logging-evkb-*/zephyr/.config",
            "crux-pilot/build/zephyr/logging-evkb-*/zephyr/zephyr.dts",
            "crux-pilot/build/zephyr/logging-evkb-*/zephyr/zephyr.stat",
            "crux-pilot/build/zephyr/logging-evkb-*/zephyr/zephyr.map",
            "crux-pilot/build/zephyr/logging-evkb-*/zephyr/zephyr.elf",
            "crux-pilot/build/zephyr/logging-evkb-*/zephyr/zephyr.bin",
            "crux-pilot/build/evidence/logging-evkb/",
        ):
            with self.subTest(path=evidence_path):
                self.assertIn(evidence_path, workflow)

    def test_candidate_script_runs_current_configuration_native_sim_and_full_images(self):
        script = (REPO / "scripts/test-logging.sh").read_text()
        for required in (
            "tests/build/test_syslog_server.py",
            "CRUX_LOGGING_COMPOSITION_TESTS=1",
            "west twister",
            "native_sim/native/64",
            "west build",
            "-p always",
            "logging-evkb-$profile",
            "rom_report",
            "ram_report",
            "record-build-provenance.py",
            "compare-logging-evkb-resources.py",
            "build_evkb_profile uart",
            "build_evkb_profile udp-only prj.net-only.conf",
            "build_evkb_profile uart-udp prj.net-dual.conf",
        ):
            with self.subTest(fragment=required):
                self.assertIn(required, script)


if __name__ == "__main__":
    unittest.main()
