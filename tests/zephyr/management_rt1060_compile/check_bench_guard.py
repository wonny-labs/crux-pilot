#!/usr/bin/env python3
"""The bench guards of the RT1060-EVKB bench image refuse every build that is not the declared bench image (C1).

Three levels, as docs/rt1060-management-image-feasibility.md describes: bench_guard.cmake at configuration,
src/bench_guard.h at compilation, and Kconfig.bench at configuration of Kconfig. This script breaks each guard in
turn and shows that the build refuses, as tests/native/management_composition/check_ssh_guard.py does for native_sim:

  * CMake: bench_guard.cmake runs in script mode, with no Zephyr, once with a good bench and then with each
    setting broken, and must refuse with that setting's reason. The good bench must pass.
  * C: bench_guard.h and every source of the image that holds a test form are compiled with each definition removed.
  * Kconfig: Kconfig.bench is loaded with Kconfiglib over a stub of the few Zephyr symbols it names, and every
    symbol assigned under a broken condition must drop to n. Zephyr only warns about a dropped assignment, so
    bench_guard.cmake is what refuses it in a build.
  * Mutation (default; --no-mutants skips it): each guard is weakened in a copy of the files, one at a time, and the
    checks above must then fail. A guard that no check exercises would survive and fail this script.

It needs cmake, cc, and for the Kconfig level Kconfiglib (pip's `kconfiglib`, or Zephyr's scripts/kconfig found
through ZEPHYR_BASE, CRUX_WORKSPACE or the folder beside the repository). It touches no board and no network.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
KCONFIGLIB_SUBDIR = Path("scripts") / "kconfig"
# The symbols that Kconfig.bench names, with prompts so that the script can assign them, as a conf file does.
STUB_KCONFIG = """
config BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI
	bool "stub"
config BOARD_REVISION
	string "stub"
config ENTROPY_MCUX_TRNG
	bool "stub"
config TEST_RANDOM_GENERATOR
	bool "stub"
config FAKE_ENTROPY_NATIVE_SIM
	bool "stub"
config NET_DHCPV4
	bool "stub"
config NET_IPV6
	bool "stub"
config RTC
	bool "stub"
config DISK_DRIVER_RAM
	bool "stub"
config DISK_DRIVER_SDMMC
	bool "stub"
source "{kconfig_bench}"
"""


# What each refusal said, for --verbose: (level, case, the line of the refusal).
TRANSCRIPT = []


def refused_for(result, reason):
    """The build failed, and the reason is in an error line: a #warning or a note with that text does not count."""
    error_lines = [line for line in result.stderr.splitlines() if "error" in line]
    return result.returncode != 0 and any(reason in line for line in error_lines)


def squeeze(text):
    """CMake wraps its messages, so a reason is looked for with the line breaks removed."""
    return " ".join(text.split())


class Tree:
    """The files of the image that the guards live in, either the real ones or a mutated copy."""

    def __init__(self, root):
        self.root = Path(root)
        self.declaration = json.loads((self.root / "bench_declaration.json").read_text())

    @property
    def board(self):
        return self.declaration["board"]

    @property
    def address(self):
        return self.declaration["network"]["address"]

    def form(self, name):
        return self.declaration["test_forms"][name]

    def selected_forms(self):
        return [f["symbol"] for f in self.declaration["test_forms"].values() if f["state"] == "selected"]

    def reserved_forms(self):
        return [f["symbol"] for f in self.declaration["test_forms"].values() if f["state"] != "selected"]


# ---- CMake level -----------------------------------------------------------------------------------------------


def make_zephyr_base(tree, directory, marker=None, patched_text="patched", pin=None, states=None):
    """A stand-in for the build-local Zephyr copy, and a declaration copy that names its hash."""
    patch = tree.declaration["patched_zephyr"]
    base = Path(directory) / "zephyr"
    target = base / patch["patched_file"]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(patched_text)
    pin = pin or patch["zephyr_pin"]
    if marker is not False:
        (base / patch["marker_file"]).write_text(marker or f"{pin} {patch['patch_sha256']}\n")
    declaration = declared_with(tree, states)
    declaration["patched_zephyr"]["patched_file_sha256"] = hashlib.sha256(patched_text.encode()).hexdigest()
    declared = Path(directory) / "declaration.json"
    declared.write_text(json.dumps(declaration))
    return base, declared


def declared_with(tree, states=None):
    """A copy of the declaration, with the state of some test forms changed: {form name: state}."""
    declaration = json.loads(json.dumps(tree.declaration))
    for name, state in (states or {}).items():
        declaration["test_forms"][name]["state"] = state
    return declaration


def backend_settings(declaration):
    """The File provider and disk drivers that the declared file backend goes with (row C3, D2)."""
    forms = declaration["test_forms"]
    if forms["microsd_card"]["state"] == "selected":
        return {"CONFIG_CRUX_FILE_FATFS_RT1060": "y", "CONFIG_DISK_DRIVER_SDMMC": "y"}
    return {"CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": "y", "CONFIG_DISK_DRIVER_RAM": "y"}


def good_cmake_settings(tree, declaration=None):
    declaration = declaration or tree.declaration
    net = declaration["network"]
    listeners = declaration["listeners"]
    settings = {
        "CRUX_MANAGEMENT_BENCH_IMAGE": "ON",
        "CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI": "y",
        "CONFIG_BOARD_TARGET": tree.board["kconfig_target"],
        "CONFIG_BOARD_REVISION": tree.board["revision"],
        "CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE": "y",
        "CONFIG_ENTROPY_MCUX_TRNG": "y",
        "CONFIG_WOLFPSA": "y",
        "CONFIG_CRUX_SSH": "y",
        "CONFIG_CRUX_SSH_CLI": "y",
        "CONFIG_CRUX_SSH_SFTP": "y",
        "CONFIG_CRUX_WEB": "y",
        "CONFIG_CRUX_LOGGING": "y",
        "CONFIG_CRUX_WEB_BIND_ADDRESS": net["address"],
        "CONFIG_CRUX_SSH_BIND_ADDRESS": net["address"],
        "CONFIG_CRUX_WEB_PORT": str(listeners["https_port"]),
        "CONFIG_CRUX_SSH_PORT": str(listeners["ssh_port"]),
    }
    for form in declaration["test_forms"].values():
        if form["state"] == "selected":
            settings[f"CONFIG_{form['symbol']}"] = "y"
    settings.update(backend_settings(declaration))
    return settings


def cmake_cases(tree):
    """(name, how to break the good settings, the reason of the refusal)."""
    net = tree.declaration["network"]
    cases = [
        ("no declaration", {"CRUX_MANAGEMENT_BENCH_IMAGE": None}, "not a declared RT1060-EVKB bench image"),
        ("a native test image's declaration only", {"CRUX_MANAGEMENT_BENCH_IMAGE": None,
                                                    "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE": "ON"},
         "not a declared RT1060-EVKB bench image"),
        ("native_sim", {"CONFIG_BOARD_NATIVE_SIM": "y"}, "is restricted to"),
        ("another board", {"CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI": None}, "is restricted to"),
        ("another target of this board", {"CONFIG_BOARD_TARGET": "mimxrt1060_evk@B/mimxrt1062/hyperflash"},
         "is restricted to"),
        ("revision A", {"CONFIG_BOARD_REVISION": "A"}, "is restricted to"),
        ("no Kconfig declaration", {"CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE": None},
         "Kconfig declaration CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE is not set"),
        ("a test random generator", {"CONFIG_TEST_RANDOM_GENERATOR": "y"}, "over the TRNG driver"),
        ("fake entropy", {"CONFIG_FAKE_ENTROPY_NATIVE_SIM": "y"}, "over the TRNG driver"),
        ("no TRNG driver", {"CONFIG_ENTROPY_MCUX_TRNG": None}, "over the TRNG driver"),
        ("no wolfPSA", {"CONFIG_WOLFPSA": None}, "needs the Wolf profile"),
        ("the Mbed profile", {"CONFIG_MBEDTLS": "y"}, "needs the Wolf profile"),
        ("the Mbed PSA provider", {"CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS": "y"}, "needs the Wolf profile"),
        ("no SSH", {"CONFIG_CRUX_SSH": None}, "HTTPS only, and SSH with CLI and SFTP"),
        ("no command line", {"CONFIG_CRUX_SSH_CLI": None}, "HTTPS only, and SSH with CLI and SFTP"),
        ("no SFTP", {"CONFIG_CRUX_SSH_SFTP": None}, "HTTPS only, and SSH with CLI and SFTP"),
        ("no web", {"CONFIG_CRUX_WEB": None}, "HTTPS only, and SSH with CLI and SFTP"),
        ("plain HTTP", {"CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS": "y"}, "HTTPS only, and SSH with CLI and SFTP"),
        ("no Crux Logging", {"CONFIG_CRUX_LOGGING": None}, "CONFIG_CRUX_LOGGING is required"),
        ("DHCP", {"CONFIG_NET_DHCPV4": "y"}, "DHCP, IPv6 and automatic address configuration are refused"),
        ("IPv6", {"CONFIG_NET_IPV6": "y"}, "DHCP, IPv6 and automatic address configuration are refused"),
        ("automatic address configuration", {"CONFIG_NET_CONFIG_SETTINGS": "y"},
         "DHCP, IPv6 and automatic address configuration are refused"),
        ("HTTPS on every address", {"CONFIG_CRUX_WEB_BIND_ADDRESS": ""}, "HTTPS listener must listen on the bench address only"),
        ("HTTPS on 0.0.0.0", {"CONFIG_CRUX_WEB_BIND_ADDRESS": "0.0.0.0"},
         "HTTPS listener must listen on the bench address only"),
        ("HTTPS on another address", {"CONFIG_CRUX_WEB_BIND_ADDRESS": "192.168.77.3"},
         "HTTPS listener must listen on the bench address only"),
        ("SSH on every address", {"CONFIG_CRUX_SSH_BIND_ADDRESS": ""}, "SSH listener must listen on the bench address only"),
        ("SSH on 0.0.0.0", {"CONFIG_CRUX_SSH_BIND_ADDRESS": "0.0.0.0"},
         "SSH listener must listen on the bench address only"),
        ("SSH on the peer's address", {"CONFIG_CRUX_SSH_BIND_ADDRESS": tree.declaration["network"]["peer"]},
         "SSH listener must listen on the bench address only"),
        ("another HTTPS port", {"CONFIG_CRUX_WEB_PORT": "8443"}, "listener ports are the declared ones"),
        ("another SSH port", {"CONFIG_CRUX_SSH_PORT": "2222"}, "listener ports are the declared ones"),
        ("a time source", {"CONFIG_RTC": "y"}, "has no time source"),
        ("mkfs on mount", {"CONFIG_FS_FATFS_MOUNT_MKFS": "y"}, "never formats a card"),
        ("an unknown bench test form", {"CONFIG_CRUX_BENCH_INVENTED_FORM": "y"},
         "does not list"),
        # The file backend (row C3, D2): the provider, the disk drivers and the form agree, and no card is reachable.
        ("no RAM disk provider", {"CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": None},
         "needs the RAM disk File provider and the RAM disk driver"),
        ("no RAM disk driver", {"CONFIG_DISK_DRIVER_RAM": None},
         "needs the RAM disk File provider and the RAM disk driver"),
        ("the SD card driver in the RAM disk image", {"CONFIG_DISK_DRIVER_SDMMC": "y"}, "has no card"),
        ("the USDHC host in the RAM disk image", {"CONFIG_IMX_USDHC": "y"}, "has no card"),
        ("the microSD provider in the RAM disk image", {"CONFIG_CRUX_FILE_FATFS_RT1060": "y"}, "has no card"),
        ("a flash disk in the RAM disk image", {"CONFIG_DISK_DRIVER_FLASH": "y"}, "has no card"),
        ("both file forms declared selected and set",
         {"DECLARATION_STATES": {"microsd_card": "selected"}, "CONFIG_CRUX_BENCH_FILE_MICROSD": "y"},
         "has exactly one file backend"),
        ("no file form declared selected and none set",
         {"DECLARATION_STATES": {"ram_disk": "reserved"}, "CONFIG_CRUX_BENCH_FILE_RAM_DISK": None,
          "CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": None, "CONFIG_DISK_DRIVER_RAM": None},
         "needs a file backend"),
        ("the microSD form declared selected, with no microSD provider",
         {"DECLARATION_STATES": {"ram_disk": "reserved", "microsd_card": "selected"},
          "CONFIG_CRUX_FILE_FATFS_RT1060": None}, "needs the microSD File provider and the SD card driver"),
        ("the microSD form declared selected, with no SD card driver",
         {"DECLARATION_STATES": {"ram_disk": "reserved", "microsd_card": "selected"},
          "CONFIG_DISK_DRIVER_SDMMC": None}, "needs the microSD File provider and the SD card driver"),
        ("the microSD form declared selected, with the RAM disk provider",
         {"DECLARATION_STATES": {"ram_disk": "reserved", "microsd_card": "selected"},
          "CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": "y"}, "has no RAM disk"),
        ("the microSD form declared selected, with the RAM disk driver",
         {"DECLARATION_STATES": {"ram_disk": "reserved", "microsd_card": "selected"},
          "CONFIG_DISK_DRIVER_RAM": "y"}, "has no RAM disk"),
    ]
    for name, form in tree.declaration["test_forms"].items():
        symbol = f"CONFIG_{form['symbol']}"
        if form["state"] == "selected":
            cases.append((f"the test form {name} not set", {symbol: None}, "is declared selected"))
        else:
            cases.append((f"the reserved test form {name} set", {symbol: "y"}, "is reserved and nothing implements it"))
    cases.extend([
        ("no build-local Zephyr copy", {"ZEPHYR_BASE_BROKEN": "no marker"}, "is not the build-local Zephyr copy"),
        ("a marker without the patched file", {"ZEPHYR_BASE_BROKEN": "no file"}, "is not the build-local Zephyr copy"),
        ("a Zephyr copy on another pin", {"ZEPHYR_BASE_BROKEN": "pin"}, "is not the build-local Zephyr copy"),
        ("a Zephyr copy with another patch", {"ZEPHYR_BASE_BROKEN": "file"}, "is not the build-local Zephyr copy"),
    ])
    return cases


def run_cmake(tree, settings, zephyr_base, declaration):
    with tempfile.NamedTemporaryFile("w", suffix=".cmake", delete=False) as script:
        # The version of the image's CMakeLists.txt, so that policies are as in a build and not as CMake defaults them
        # in script mode, which differs between CMake versions.
        script.write("cmake_minimum_required(VERSION 3.20.0)\n")
        for name, value in settings.items():
            script.write(f'set({name} "{value}")\n')
        script.write(f'set(ZEPHYR_BASE "{zephyr_base}")\n')
        script.write(f'set(CRUX_BENCH_DECLARATION_OVERRIDE "{declaration}")\n')
        script.write(f'include("{tree.root / "bench_guard.cmake"}")\n')
        script.write("crux_bench_guard()\n")
        script.write('message(STATUS "bench guard passed")\n')
    result = subprocess.run(["cmake", "-P", script.name], capture_output=True, text=True, check=False)
    Path(script.name).unlink()
    return result


def check_cmake(tree):
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        good_dir = Path(scratch) / "good"
        good_dir.mkdir()
        zephyr, declaration = make_zephyr_base(tree, good_dir)
        control = run_cmake(tree, good_cmake_settings(tree), zephyr, declaration)
        if control.returncode != 0 or "bench guard passed" not in control.stderr + control.stdout:
            failures.append(f"bench_guard.cmake refused the declared bench: {squeeze(control.stderr)}")
        # The microSD state of the declaration, which a later run would take, is accepted by the same guard with
        # the card's provider and driver, and is what the RAM disk settings above are refused under.
        card_dir = Path(scratch) / "card"
        card_dir.mkdir()
        card_states = {"ram_disk": "reserved", "microsd_card": "selected"}
        zephyr, declaration = make_zephyr_base(tree, card_dir, states=card_states)
        card = run_cmake(tree, good_cmake_settings(tree, declared_with(tree, card_states)), zephyr, declaration)
        if card.returncode != 0 or "bench guard passed" not in card.stderr + card.stdout:
            failures.append(f"bench_guard.cmake refused the microSD state of the declaration: {squeeze(card.stderr)}")
        else:
            TRANSCRIPT.append(("CMake", "control: the microSD state of the declaration, with the card's provider and driver", "accepted"))
        zephyr, declaration = make_zephyr_base(tree, card_dir, states=card_states)
        under_card = run_cmake(tree, good_cmake_settings(tree), zephyr, declaration)
        if under_card.returncode == 0 or "is declared selected" not in squeeze(under_card.stderr):
            failures.append("bench_guard.cmake accepted the RAM disk settings under the microSD state: "
                            f"{squeeze(under_card.stderr)}")
        for index, (name, change, reason) in enumerate(cmake_cases(tree)):
            states = change.get("DECLARATION_STATES")
            settings = good_cmake_settings(tree, declared_with(tree, states))
            case_dir = Path(scratch) / f"case{index}"
            case_dir.mkdir()
            broken = change.get("ZEPHYR_BASE_BROKEN")
            if broken == "no marker":
                zephyr, declaration = make_zephyr_base(tree, case_dir, marker=False)
            elif broken == "no file":
                zephyr, declaration = make_zephyr_base(tree, case_dir)
                (zephyr / tree.declaration["patched_zephyr"]["patched_file"]).unlink()
            elif broken == "pin":
                zephyr, declaration = make_zephyr_base(tree, case_dir, pin="0" * 40)
            elif broken == "file":
                zephyr, declaration = make_zephyr_base(tree, case_dir)
                (zephyr / tree.declaration["patched_zephyr"]["patched_file"]).write_text("another file")
            else:
                zephyr, declaration = make_zephyr_base(tree, case_dir, states=states)
            for key, value in change.items():
                if key in ("ZEPHYR_BASE_BROKEN", "DECLARATION_STATES"):
                    continue
                if value is None:
                    settings.pop(key, None)
                else:
                    settings[key] = value
            result = run_cmake(tree, settings, zephyr, declaration)
            if result.returncode == 0 or reason not in squeeze(result.stderr):
                failures.append(f"bench_guard.cmake, {name}: not refused for its reason ({reason}): "
                                f"rc={result.returncode} {squeeze(result.stderr)}")
            else:
                said = squeeze(result.stderr).split("CMake Error at", 1)[-1]
                TRANSCRIPT.append(("CMake", name, said.split(":", 1)[-1].split("(message):", 1)[-1].split(" Call Stack")[0].strip()))
    failures += check_manifest(tree)
    return failures


def run_manifest(tree, directory, hash_value):
    """crux_bench_manifest() in script mode, with the first account's hash recorded as prepare would, or not."""
    root = HERE.parents[2]
    with tempfile.NamedTemporaryFile("w", suffix=".cmake", delete=False) as script:
        script.write("cmake_minimum_required(VERSION 3.20.0)\n")
        script.write(f'set(CRUX_ROOT "{root}")\nset(CMAKE_BINARY_DIR "{directory}")\n')
        script.write(f'set(CRUX_BENCH_DECLARATION_OVERRIDE "{tree.root / "bench_declaration.json"}")\n')
        for symbol in tree.selected_forms():
            script.write(f'set(CONFIG_{symbol} "y")\n')
        if hash_value is not None:
            script.write(f'set_property(GLOBAL PROPERTY CRUX_BENCH_FIRST_ACCOUNT_SHA256 "{hash_value}")\n')
        script.write(f'include("{tree.root / "bench_guard.cmake"}")\ncrux_bench_manifest()\n')
    result = subprocess.run(["cmake", "-P", script.name], capture_output=True, text=True, check=False)
    Path(script.name).unlink()
    return result


def check_manifest(tree):
    """The manifest records the first account's file hash, and refuses to be written without one (row C4)."""
    failures = []
    expected = "ab" * 32
    with tempfile.TemporaryDirectory() as scratch:
        good = Path(scratch) / "good"
        good.mkdir()
        result = run_manifest(tree, good, expected)
        manifest = good / "bench-manifest.json"
        if result.returncode != 0 or not manifest.is_file():
            return [f"crux_bench_manifest() failed with the first account's hash recorded: {squeeze(result.stderr)}"]
        record = json.loads(manifest.read_text())
        account = record.get("first_account", {})
        declared = tree.declaration["first_account"]
        if (account.get("file_sha256"), account.get("user"), account.get("role")) != (expected, declared["user"],
                                                                                      declared["role"]):
            failures.append(f"the manifest does not record the first account's file hash, name and role: {account}")
        if account.get("plaintext_in_image") is not True:
            failures.append("the manifest does not say that the first account's plaintext is in the image")
        if record["test_forms"]["first_account_file"].get("set") is not True:
            failures.append("the manifest does not show the first account form as set")
        bare = Path(scratch) / "bare"
        bare.mkdir()
        result = run_manifest(tree, bare, None)
        if result.returncode == 0 or "was not checked before the manifest" not in squeeze(result.stderr):
            failures.append("crux_bench_manifest() was written without a checked first account password file: "
                            f"rc={result.returncode} {squeeze(result.stderr)}")
        else:
            TRANSCRIPT.append(("CMake", "a manifest with no checked first account file", "was not checked before the manifest"))
    return failures


# ---- C level ---------------------------------------------------------------------------------------------------

C_GUARD_MESSAGES = (
    "did not declare as the RT1060-EVKB bench image",
    "Kconfig does not declare the RT1060-EVKB bench image",
    "restricted to RT1060-EVKB revision B",
    "over the TRNG driver",
    "did not reach the compiler",
    "sets both listener bind addresses",
    "HTTPS listener must listen on the bench address only",
    "SSH listener must listen on the bench address only",
    "declared only with CONFIG_CRUX_BENCH_TEST_HOST_KEY",
    "declared only with CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE",
    "did not reach the compiler: the first account's name and role",
    "A mount never formats a card",
    "has exactly one file backend",
    "needs the RAM disk File provider and the RAM disk driver",
    "The RAM disk image has no card",
    "needs the microSD File provider and the SD card driver",
    "The microSD image has no RAM disk",
)


def good_c_definitions(tree):
    net = tree.declaration["network"]
    return {
        "CRUX_MANAGEMENT_BENCH_IMAGE": "1",
        "CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE": "1",
        "CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI": "1",
        "CONFIG_BOARD_REVISION_B": "1",
        "CONFIG_ENTROPY_MCUX_TRNG": "1",
        "CRUX_BENCH_ADDRESS": f'"{net["address"]}"',
        "CRUX_BENCH_NETMASK": f'"{net["netmask"]}"',
        "CRUX_BENCH_IMAGE_ID": f'"{tree.declaration["image_id"]}"',
        "CONFIG_CRUX_WEB_BIND_ADDRESS": f'"{net["address"]}"',
        "CONFIG_CRUX_SSH_BIND_ADDRESS": f'"{net["address"]}"',
        "CONFIG_CRUX_BENCH_TEST_HOST_KEY": "1",
        # The file backend of the declared bench: the RAM disk (row C3, D2).
        "CONFIG_CRUX_BENCH_FILE_RAM_DISK": "1",
        "CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": "1",
        "CONFIG_DISK_DRIVER_RAM": "1",
        "CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE": "1",
        "CRUX_BENCH_FIRST_ACCOUNT_USER": f'"{tree.declaration["first_account"]["user"]}"',
        "CRUX_BENCH_FIRST_ACCOUNT_ROLE": f'"{tree.declaration["first_account"]["role"]}"',
    }


def c_cases(tree):
    net = tree.declaration["network"]
    return [
        ("no CMake declaration", {"CRUX_MANAGEMENT_BENCH_IMAGE": None}, "did not declare as the RT1060-EVKB bench image"),
        ("no Kconfig declaration", {"CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE": None},
         "Kconfig does not declare the RT1060-EVKB bench image"),
        ("native_sim", {"CONFIG_BOARD_NATIVE_SIM": "1"}, "restricted to RT1060-EVKB revision B"),
        ("another board", {"CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI": None}, "restricted to RT1060-EVKB revision B"),
        ("another revision", {"CONFIG_BOARD_REVISION_B": None}, "restricted to RT1060-EVKB revision B"),
        ("a test random generator", {"CONFIG_TEST_RANDOM_GENERATOR": "1"}, "over the TRNG driver"),
        ("fake entropy", {"CONFIG_FAKE_ENTROPY_NATIVE_SIM": "1"}, "over the TRNG driver"),
        ("no TRNG driver", {"CONFIG_ENTROPY_MCUX_TRNG": None}, "over the TRNG driver"),
        ("no bench address", {"CRUX_BENCH_ADDRESS": None}, "did not reach the compiler"),
        ("no bench netmask", {"CRUX_BENCH_NETMASK": None}, "did not reach the compiler"),
        ("no image identity", {"CRUX_BENCH_IMAGE_ID": None}, "did not reach the compiler"),
        ("no HTTPS bind address", {"CONFIG_CRUX_WEB_BIND_ADDRESS": None}, "sets both listener bind addresses"),
        ("no SSH bind address", {"CONFIG_CRUX_SSH_BIND_ADDRESS": None}, "sets both listener bind addresses"),
        ("HTTPS on every address", {"CONFIG_CRUX_WEB_BIND_ADDRESS": '""'},
         "HTTPS listener must listen on the bench address only"),
        ("HTTPS on another address", {"CONFIG_CRUX_WEB_BIND_ADDRESS": '"192.168.77.3"'},
         "HTTPS listener must listen on the bench address only"),
        ("SSH on every address", {"CONFIG_CRUX_SSH_BIND_ADDRESS": '""'},
         "SSH listener must listen on the bench address only"),
        ("SSH on 0.0.0.0", {"CONFIG_CRUX_SSH_BIND_ADDRESS": '"0.0.0.0"'},
         "SSH listener must listen on the bench address only"),
        ("mkfs on mount", {"CONFIG_FS_FATFS_MOUNT_MKFS": "1"}, "A mount never formats a card"),
        ("both file forms", {"CONFIG_CRUX_BENCH_FILE_MICROSD": "1"}, "has exactly one file backend"),
        ("the RAM disk form without its provider", {"CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": None},
         "needs the RAM disk File provider and the RAM disk driver"),
        ("the RAM disk form without its driver", {"CONFIG_DISK_DRIVER_RAM": None},
         "needs the RAM disk File provider and the RAM disk driver"),
        ("the SD card driver in the RAM disk image", {"CONFIG_DISK_DRIVER_SDMMC": "1"}, "The RAM disk image has no card"),
        ("the USDHC host in the RAM disk image", {"CONFIG_IMX_USDHC": "1"}, "The RAM disk image has no card"),
        ("the microSD provider in the RAM disk image", {"CONFIG_CRUX_FILE_FATFS_RT1060": "1"},
         "The RAM disk image has no card"),
        ("a flash disk in the RAM disk image", {"CONFIG_DISK_DRIVER_FLASH": "1"}, "The RAM disk image has no card"),
    ] + microsd_c_cases()


def card_definitions(good):
    """The definitions of a build that took the microSD state of the declaration, as a later run would."""
    card = dict(good)
    card.update({"CONFIG_CRUX_BENCH_FILE_RAM_DISK": None, "CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": None,
                 "CONFIG_DISK_DRIVER_RAM": None, "CONFIG_CRUX_BENCH_FILE_MICROSD": "1",
                 "CONFIG_CRUX_FILE_FATFS_RT1060": "1", "CONFIG_DISK_DRIVER_SDMMC": "1"})
    return card


def microsd_c_cases():
    """Cases that break the microSD state: a change to the card definitions, and the reason."""
    return [
        ("the microSD form without its provider", {"CONFIG_CRUX_FILE_FATFS_RT1060": None, "__card": True},
         "needs the microSD File provider and the SD card driver"),
        ("the microSD form without the SD card driver", {"CONFIG_DISK_DRIVER_SDMMC": None, "__card": True},
         "needs the microSD File provider and the SD card driver"),
        ("the microSD form with the RAM disk provider", {"CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK": "1", "__card": True},
         "The microSD image has no RAM disk"),
        ("the microSD form with the RAM disk driver", {"CONFIG_DISK_DRIVER_RAM": "1", "__card": True},
         "The microSD image has no RAM disk"),
    ]


def compile_c(tree, path, definitions, header_only=False):
    command = ["cc", "-std=c17", "-fsyntax-only", "-x", "c", str(path), f"-I{tree.root / 'src'}",
               f"-I{tree.root / 'first_account'}"]
    command += [f"-D{name}={value}" for name, value in definitions.items() if value is not None]
    return subprocess.run(command, capture_output=True, text=True, check=False)


def bench_sources(tree):
    return sorted((tree.root / "src").glob("*.c"))


def check_c(tree):
    failures = []
    guard = tree.root / "src" / "bench_guard.h"
    good = good_c_definitions(tree)
    control = compile_c(tree, guard, good)
    if control.returncode != 0:
        failures.append(f"bench_guard.h refused the declared bench: {control.stderr}")
    card = compile_c(tree, guard, card_definitions(good))
    if card.returncode != 0:
        failures.append(f"bench_guard.h refused the microSD state of the declaration: {card.stderr}")
    for name, change, reason in c_cases(tree):
        change = dict(change)
        definitions = dict(card_definitions(good) if change.pop("__card", False) else good)
        definitions.update(change)
        result = compile_c(tree, guard, definitions)
        if not refused_for(result, reason):
            failures.append(f"bench_guard.h, {name}: not refused for its reason ({reason}): {result.stderr}")
        else:
            said = next(line for line in result.stderr.splitlines() if "error" in line and reason in line)
            TRANSCRIPT.append(("C", name, said.split("error:", 1)[-1].strip()))
    # Every source of the image includes the guard, and is refused by it without the declaration and not with it. A
    # source also stops at a Zephyr or Crux header that this host lacks: that is how the allowed case ends.
    for source in bench_sources(tree):
        if '#include "bench_guard.h"' not in source.read_text():
            failures.append(f"{source.name} does not include bench_guard.h")
            continue
        refused = compile_c(tree, source, {k: v for k, v in good.items() if k != "CRUX_MANAGEMENT_BENCH_IMAGE"})
        if not refused_for(refused, C_GUARD_MESSAGES[0]):
            failures.append(f"{source.name} on a build that is not a declared bench image: not refused by the guard: "
                            f"{refused.stderr}")
        allowed = compile_c(tree, source, good)
        seen = [m for m in C_GUARD_MESSAGES if m in allowed.stderr]  # a warning counts here too
        if seen:
            failures.append(f"{source.name} in the declared bench image: refused by the guard ({seen}): {allowed.stderr}")
    # The test host key form is refused without its symbol, in the one source that holds it.
    for form_name, source, message in (("test_host_key", "ssh_target.c", "declared only with CONFIG_CRUX_BENCH_TEST_HOST_KEY"),
                                       ("first_account_file", "bench_first_account.c",
                                        "declared only with CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE")):
        form = tree.form(form_name)["symbol"]
        without = {k: v for k, v in good.items() if k != f"CONFIG_{form}"}
        refused = compile_c(tree, tree.root / "src" / source, without)
        if not refused_for(refused, message):
            failures.append(f"{source} without CONFIG_{form}: not refused: {refused.stderr}")
        else:
            TRANSCRIPT.append(("C", f"{source} without its form symbol", message))
    # The first account's name and role come from the declaration, and a source that lacks them is refused.
    for name in ("CRUX_BENCH_FIRST_ACCOUNT_USER", "CRUX_BENCH_FIRST_ACCOUNT_ROLE"):
        refused = compile_c(tree, tree.root / "src" / "bench_first_account.c",
                            {k: v for k, v in good.items() if k != name})
        if not refused_for(refused, "did not reach the compiler: the first account's name and role"):
            failures.append(f"bench_first_account.c without {name}: not refused: {refused.stderr}")
    return failures


# ---- Kconfig level ---------------------------------------------------------------------------------------------


def load_kconfiglib():
    try:
        import kconfiglib  # noqa: PLC0415
        return kconfiglib
    except ImportError:
        pass
    candidates = []
    for variable in ("ZEPHYR_BASE",):
        if os.environ.get(variable):
            candidates.append(Path(os.environ[variable]))
    if os.environ.get("CRUX_WORKSPACE"):
        candidates.append(Path(os.environ["CRUX_WORKSPACE"]) / "zephyr")
    candidates.append(HERE.parents[3].parent / "zephyr")
    for base in candidates:
        path = base / KCONFIGLIB_SUBDIR
        if (path / "kconfiglib.py").is_file():
            sys.path.insert(0, str(path))
            import kconfiglib  # noqa: PLC0415
            return kconfiglib
    return None


def kconfig_sym(kconf, name):
    return kconf.syms[name]


def check_kconfig(tree, kconfiglib):
    failures = []
    bench = tree.root / "Kconfig.bench"
    declared = {f["symbol"] for f in tree.declaration["test_forms"].values()}

    def load():
        directory = tempfile.mkdtemp()
        stub = Path(directory) / "Kconfig"
        stub.write_text(STUB_KCONFIG.format(kconfig_bench=bench))
        return kconfiglib.Kconfig(str(stub), warn=False), directory

    def good_board(kconf):
        kconf.syms["BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI"].set_value("y")
        kconf.syms["BOARD_REVISION"].set_value(tree.board["revision"])
        kconf.syms["ENTROPY_MCUX_TRNG"].set_value("y")
        kconf.syms["DISK_DRIVER_RAM"].set_value("y")  # the RAM disk's driver, which the declared file backend needs

    def card_board(kconf):
        good_board(kconf)
        kconf.syms["DISK_DRIVER_RAM"].set_value("n")
        kconf.syms["DISK_DRIVER_SDMMC"].set_value("y")

    kconf, directory = load()
    defined = {name for name in kconf.syms if name.startswith("CRUX_BENCH_")}
    if defined != declared:
        failures.append(f"Kconfig.bench and bench_declaration.json disagree on the test forms: only in Kconfig "
                        f"{sorted(defined - declared)}, only in the declaration {sorted(declared - defined)}")
    # The control: the declared bench accepts the declaration and every test form.
    good_board(kconf)
    kconf.syms["CRUX_MANAGEMENT_BENCH_IMAGE"].set_value("y")
    for name in sorted(declared):
        kconf.syms[name].set_value("y")
    if kconf.syms["CRUX_MANAGEMENT_BENCH_IMAGE"].str_value != "y":
        failures.append("Kconfig.bench refuses the declaration on the declared board")
    for name in sorted(declared):
        # The microSD form excludes the RAM disk form: with both set it drops, which is the rule (checked below).
        if kconf.syms[name].str_value != "y" and name != "CRUX_BENCH_FILE_MICROSD":
            failures.append(f"Kconfig.bench refuses {name} under the declaration")
    if kconf.syms["CRUX_BENCH_FILE_MICROSD"].str_value != "n":
        failures.append("Kconfig.bench accepts the microSD form beside the RAM disk form")
    shutil.rmtree(directory)

    # Each broken condition: the declaration drops to n, and so does every test form with it.
    broken = [
        ("another board", lambda k: k.syms["BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI"].set_value("n")),
        ("revision A", lambda k: k.syms["BOARD_REVISION"].set_value("A")),
        ("no TRNG driver", lambda k: k.syms["ENTROPY_MCUX_TRNG"].set_value("n")),
        ("a test random generator", lambda k: k.syms["TEST_RANDOM_GENERATOR"].set_value("y")),
        ("fake entropy", lambda k: k.syms["FAKE_ENTROPY_NATIVE_SIM"].set_value("y")),
        ("DHCP", lambda k: k.syms["NET_DHCPV4"].set_value("y")),
        ("IPv6", lambda k: k.syms["NET_IPV6"].set_value("y")),
    ]
    for name, apply in broken:
        kconf, directory = load()
        good_board(kconf)
        apply(kconf)
        kconf.syms["CRUX_MANAGEMENT_BENCH_IMAGE"].set_value("y")
        for form in sorted(declared):
            kconf.syms[form].set_value("y")
        if kconf.syms["CRUX_MANAGEMENT_BENCH_IMAGE"].str_value != "n":
            failures.append(f"Kconfig.bench, {name}: the declaration is not refused")
        for form in sorted(declared):
            if kconf.syms[form].str_value != "n":
                failures.append(f"Kconfig.bench, {name}: {form} is accepted without the declaration")
        shutil.rmtree(directory)
    # Every test form alone, with the declaration not set on a board that would allow it.
    for form in sorted(declared):
        kconf, directory = load()
        (card_board if form == "CRUX_BENCH_FILE_MICROSD" else good_board)(kconf)
        kconf.syms[form].set_value("y")
        if kconf.syms[form].str_value != "n":
            failures.append(f"Kconfig.bench: {form} is accepted without the declaration")
        shutil.rmtree(directory)
    # The file backend (row C3, D2). Each pair is the board and what is broken, and the form that must drop.
    def under_declaration(board, forms, change):
        kconf, directory = load()
        board(kconf)
        change(kconf)
        kconf.syms["CRUX_MANAGEMENT_BENCH_IMAGE"].set_value("y")
        for form in forms:
            kconf.syms[form].set_value("y")
        values = {name: kconf.syms[name].str_value for name in declared}
        shutil.rmtree(directory)
        return values

    ram, card = "CRUX_BENCH_FILE_RAM_DISK", "CRUX_BENCH_FILE_MICROSD"
    if under_declaration(card_board, [card], lambda k: None)[card] != "y":
        failures.append("Kconfig.bench refuses the microSD form on the card's drivers: the state a later run takes")
    for name, board, forms, change, dropped in (
            ("the RAM disk form with the SD card driver", good_board, [ram],
             lambda k: k.syms["DISK_DRIVER_SDMMC"].set_value("y"), ram),
            ("the RAM disk form without the RAM disk driver", good_board, [ram],
             lambda k: k.syms["DISK_DRIVER_RAM"].set_value("n"), ram),
            ("the microSD form with the RAM disk driver", card_board, [card],
             lambda k: k.syms["DISK_DRIVER_RAM"].set_value("y"), card),
            ("the microSD form without the SD card driver", card_board, [card],
             lambda k: k.syms["DISK_DRIVER_SDMMC"].set_value("n"), card),
            ("the microSD form beside the RAM disk form", good_board, [ram, card], lambda k: None, card)):
        values = under_declaration(board, forms, change)
        if values[dropped] != "n":
            failures.append(f"Kconfig.bench, {name}: {dropped} is not dropped")
    # A time source and the no-time-source form exclude each other.
    kconf, directory = load()
    good_board(kconf)
    kconf.syms["CRUX_MANAGEMENT_BENCH_IMAGE"].set_value("y")
    kconf.syms["RTC"].set_value("y")
    kconf.syms["CRUX_BENCH_NO_TIME_SOURCE"].set_value("y")
    if kconf.syms["CRUX_BENCH_NO_TIME_SOURCE"].str_value != "n":
        failures.append("Kconfig.bench: the no-time-source form is accepted with CONFIG_RTC")
    shutil.rmtree(directory)
    return failures


# ---- Mutation --------------------------------------------------------------------------------------------------


def copy_tree(source_root, destination):
    for name in ("bench_declaration.json", "bench_guard.cmake", "Kconfig.bench"):
        shutil.copy(Path(source_root) / name, destination / name)
    shutil.copytree(Path(source_root) / "src", destination / "src")
    shutil.copytree(Path(source_root) / "first_account", destination / "first_account")


def replace_nth(text, needle, replacement, n):
    positions = [m.start() for m in re.finditer(re.escape(needle), text)]
    if n >= len(positions):
        return None
    at = positions[n]
    return text[:at] + replacement + text[at + len(needle):]


def mutants(root):
    """Yield (description, relative file, mutated text, group). Each weakens one guard."""
    root = Path(root)
    cmake = (root / "bench_guard.cmake").read_text()
    count = cmake.count("message(FATAL_ERROR")
    for index in range(count):
        line = cmake[:[m.start() for m in re.finditer(re.escape("message(FATAL_ERROR"), cmake)][index]].count("\n") + 1
        yield (f"bench_guard.cmake: the refusal at line {line} only logs", "bench_guard.cmake",
               replace_nth(cmake, "message(FATAL_ERROR", "message(STATUS", index), "cmake")
    header = (root / "src" / "bench_guard.h").read_text()
    directives = [m.start() for m in re.finditer(r"^#error", header, re.MULTILINE)]
    for index, at in enumerate(directives):
        line = header[:at].count("\n") + 1
        yield (f"bench_guard.h: the #error at line {line} only warns", "src/bench_guard.h",
               header[:at] + "#warning" + header[at + len("#error"):], "c")
    for index in range(header.count("_Static_assert(")):
        yield (f"bench_guard.h: static assertion {index + 1} always holds", "src/bench_guard.h",
               replace_nth(header, "_Static_assert(", "_Static_assert(1 || ", index), "c")
    for source in sorted((root / "src").glob("*.c")):
        text = source.read_text()
        yield (f"{source.name}: the include of bench_guard.h is removed", f"src/{source.name}",
               text.replace('#include "bench_guard.h"\n', "", 1), "c")
    for name, label in (("ssh_target.c", "the test host key form"), ("bench_first_account.c", "the first account form")):
        text = (root / "src" / name).read_text()
        for index, found in enumerate(re.finditer(r"^#error", text, re.MULTILINE)):
            at = found.start()
            yield (f"{name}: {label} #error {index + 1} only warns", f"src/{name}",
                   text[:at] + "#warning" + text[at + len("#error"):], "c")
    kconfig = (root / "Kconfig.bench").read_text().splitlines(keepends=True)
    for number, line in enumerate(kconfig):
        if line.strip().startswith("depends on"):
            text = "".join(kconfig[:number] + kconfig[number + 1:])
            yield (f"Kconfig.bench: line {number + 1} ({line.strip()}) is removed", "Kconfig.bench", text, "kconfig")
    # The block that puts every test form under the declaration: its `if` and its `endif` go together.
    opening = next(i for i, line in enumerate(kconfig) if line.strip() == "if CRUX_MANAGEMENT_BENCH_IMAGE")
    closing = next(i for i, line in enumerate(kconfig) if line.startswith("endif"))
    text = "".join(line for i, line in enumerate(kconfig) if i not in (opening, closing))
    yield ("Kconfig.bench: the block that puts the test forms under the declaration is removed", "Kconfig.bench",
           text, "kconfig")


def check_mutants(tree, kconfiglib):
    failures = []
    results = []
    for description, relative, text, group in mutants(tree.root):
        if text is None:
            failures.append(f"mutation not applicable: {description}")
            continue
        with tempfile.TemporaryDirectory() as scratch:
            copy = Path(scratch)
            copy_tree(tree.root, copy)
            (copy / relative).write_text(text)
            mutated = Tree(copy)
            if group == "cmake":
                found = check_cmake(mutated)
            elif group == "c":
                found = check_c(mutated)
            elif kconfiglib is not None:
                found = check_kconfig(mutated, kconfiglib)
            else:
                results.append((description, "skipped: no Kconfiglib"))
                continue
        if found:
            results.append((description, "killed"))
        else:
            results.append((description, "SURVIVED"))
            failures.append(f"mutation survived: {description}")
    return failures, results


# ---- Main ------------------------------------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--no-mutants", action="store_true", help="skip the mutation checks")
    parser.add_argument("--skip-kconfig", action="store_true", help="skip the Kconfig level (it is reported as skipped)")
    parser.add_argument("--verbose", action="store_true", help="list every mutant and its result")
    args = parser.parse_args()
    tree = Tree(HERE)
    kconfiglib = None if args.skip_kconfig else load_kconfiglib()
    failures = []
    levels = []
    for name, check in (("CMake", lambda: check_cmake(tree)), ("C", lambda: check_c(tree))):
        found = check()
        failures += found
        levels.append(f"{name}: {'ok' if not found else 'FAILED'}")
    if kconfiglib is None:
        levels.append("Kconfig: SKIPPED (no Kconfiglib" + ("; --skip-kconfig" if args.skip_kconfig else "") + ")")
    else:
        found = check_kconfig(tree, kconfiglib)
        failures += found
        levels.append(f"Kconfig: {'ok' if not found else 'FAILED'}")
    if args.verbose:
        for level, case, said in TRANSCRIPT:
            print(f"  refused  {level:5} {case}: {said}")
    TRANSCRIPT.clear()
    if not args.no_mutants:
        found, results = check_mutants(tree, kconfiglib)
        failures += found
        killed = sum(1 for _, state in results if state == "killed")
        skipped = sum(1 for _, state in results if state.startswith("skipped"))
        levels.append(f"mutants: {killed} of {len(results)} killed" + (f", {skipped} skipped" if skipped else ""))
        if args.verbose:
            for description, state in results:
                print(f"  {state:8} {description}")
    for failure in failures:
        print(failure, file=sys.stderr)
    print("; ".join(levels))
    if failures:
        return 1
    if kconfiglib is None and not args.skip_kconfig:
        print("Kconfiglib was not found: the Kconfig level did not run", file=sys.stderr)
        return 2
    levels_run = "in CMake, in C and in Kconfig" if kconfiglib is not None else "in CMake and in C (Kconfig skipped)"
    print(f"control ok: the bench image refuses every build that is not the declared RT1060-EVKB bench, {levels_run}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
