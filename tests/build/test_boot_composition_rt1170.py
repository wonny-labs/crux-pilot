"""Check the board files of the RT1170-EVK boot composition and the build check that uses them.

The composition is MCUboot RAM load with revert on the RT1170-EVK (`mimxrt1170_evk@A//cm7`),
build only. It mirrors the RT1060-EVK Rev B composition (`test_boot_composition.py`). Its values
are STARTING VALUES from the RT1170 design note, "RT1170-EVK boot composition: memory-map
choices", approved as recommended by the project owner on 15 November 2026. They are corrected
by the first CI build. These rules read files only and use the standard library, because `host.yml` runs this
directory without west or PyYAML. No build ran for this board.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
BOARD = "mimxrt1170_evk_mimxrt1176_cm7_A"
BOARD_DIR = REPO / "zephyr/boot/boards" / BOARD
PAGE = REPO / "docs/boot-composition-rt1170.md"
SCRIPT = REPO / "scripts/test-boot-composition-rt1170.sh"
ZEPHYR_WORKFLOW = REPO / ".github/workflows/zephyr.yml"
FILES = ("mcuboot.conf", "mcuboot.overlay", "app.conf", "app.overlay")

# Starting values of the design note. ocram2 is `ocram@202c0000`, 512 KiB, in nxp_rt1170.dtsi.
OCRAM2_START = 0x202C0000
OCRAM2_SIZE = 512 * 1024
RETAINED_SIZE = 0x100
RETAINED_START = OCRAM2_START + OCRAM2_SIZE - RETAINED_SIZE
TRIMMED_OCRAM2_SIZE = OCRAM2_SIZE - RETAINED_SIZE
SDRAM_START = 0x80000000
SDRAM_SIZE = 64 * 1024 * 1024


def read(path):
    return path.read_text(encoding="utf-8")


def reg_cells(text):
    """Return every `reg = <a b>;` of a devicetree text as a tuple of integers."""
    return [tuple(int(v, 16) for v in m.split()) for m in re.findall(r"reg = <([^>]+)>;", text)]


class ArithmeticTest(unittest.TestCase):
    def test_the_starting_values_agree_with_each_other(self):
        self.assertEqual(0x2033FF00, RETAINED_START)
        self.assertEqual(0x7FF00, TRIMMED_OCRAM2_SIZE)
        self.assertEqual(67108864, SDRAM_SIZE)

    def test_the_numbers_in_the_files_agree_with_the_arithmetic(self):
        # Read from the files, so that a change to one of them can fail this test.
        text = read(BOARD_DIR / "mcuboot.overlay")
        retained = re.search(r"sram@([0-9a-f]+) \{[^}]*?reg = <(0x[0-9a-f]+) (0x[0-9a-f]+)>;", text)
        self.assertIsNotNone(retained)
        node, start, size = (int(v, 16) for v in retained.groups())
        self.assertEqual(node, start)
        trimmed = re.search(r"&ocram2\s*\{\s*reg = <(0x[0-9a-f]+) (0x[0-9a-f]+)>;", text)
        self.assertIsNotNone(trimmed)
        ocram2_start, ocram2_size = (int(v, 16) for v in trimmed.groups())
        self.assertEqual(start, ocram2_start + ocram2_size)
        self.assertEqual(OCRAM2_SIZE, ocram2_size + size)


class BoardFilesTest(unittest.TestCase):
    def test_the_four_files_exist(self):
        for name in FILES:
            self.assertTrue((BOARD_DIR / name).is_file(), f"{BOARD_DIR / name} is missing")

    def test_the_files_say_that_the_values_are_starting_values(self):
        for name in FILES:
            text = read(BOARD_DIR / name)
            self.assertIn("starting value", text.lower(), name)
            self.assertIn("first CI build", text, name)

    def test_mcuboot_executes_from_sdram_and_shares_boot_information(self):
        lines = read(BOARD_DIR / "mcuboot.conf").splitlines()
        for line in (
            f"CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START={SDRAM_START:#x}",
            f"CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE={SDRAM_SIZE}",
            "CONFIG_RETAINED_MEM=y",
            "CONFIG_RETENTION=y",
            "CONFIG_BOOT_SHARE_DATA=y",
            "CONFIG_BOOT_SHARE_DATA_BOOTINFO=y",
            "CONFIG_BOOT_SHARE_BACKEND_RETENTION=y",
        ):
            self.assertIn(line, lines)

    def test_both_overlays_declare_the_same_retained_region(self):
        regions = []
        for name in ("mcuboot.overlay", "app.overlay"):
            text = read(BOARD_DIR / name)
            cells = reg_cells(text)
            # The retained node and its boot-information child, then the trimmed ocram2.
            self.assertIn((RETAINED_START, RETAINED_SIZE), cells, name)
            self.assertIn((0x0, RETAINED_SIZE), cells, name)
            self.assertIn(f"sram@{RETAINED_START:x}", text, name)
            self.assertIn(f"ranges = <0x0 {RETAINED_START:#x} {RETAINED_SIZE:#x}>;", text, name)
            regions.append(cells)
        self.assertEqual(regions[0], regions[1])

    def test_both_overlays_trim_ocram2_by_exactly_the_retained_region(self):
        for name in ("mcuboot.overlay", "app.overlay"):
            text = read(BOARD_DIR / name)
            match = re.search(r"&ocram2\s*\{\s*reg = <([^>]+)>;", text)
            self.assertIsNotNone(match, f"{name} does not trim &ocram2")
            start, size = (int(v, 16) for v in match.group(1).split())
            self.assertEqual(OCRAM2_START, start, name)
            self.assertEqual(TRIMMED_OCRAM2_SIZE, size, name)
            # The trimmed region ends where the retained region begins, with no gap or overlap.
            self.assertEqual(RETAINED_START, start + size, name)

    def test_mcuboot_runs_from_the_trimmed_ocram2(self):
        text = read(BOARD_DIR / "mcuboot.overlay")
        self.assertRegex(text, r"zephyr,sram = &ocram2;")

    def test_the_application_keeps_its_own_ram_and_asks_for_the_bootloader_information(self):
        self.assertNotIn("zephyr,sram", read(BOARD_DIR / "app.overlay"))
        config = read(BOARD_DIR / "app.conf").splitlines()
        for line in (
            "CONFIG_RETAINED_MEM=y",
            "CONFIG_RETENTION=y",
            "CONFIG_RETENTION_BOOTLOADER_INFO=y",
            "CONFIG_RETENTION_BOOTLOADER_INFO_TYPE_MCUBOOT=y",
        ):
            self.assertIn(line, config)

    def test_the_retained_region_lies_outside_the_load_window(self):
        # The image is loaded into the SDRAM, and the retained region must survive the load.
        # Both ends come from the files.
        conf = dict(l.split("=", 1) for l in read(BOARD_DIR / "mcuboot.conf").splitlines() if l.startswith("CONFIG_"))
        window_start = int(conf["CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START"], 0)
        window_end = window_start + int(conf["CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE"], 0)
        for name in ("mcuboot.overlay", "app.overlay"):
            start, size = next(c for c in reg_cells(read(BOARD_DIR / name)) if c[1] == RETAINED_SIZE and c[0])
            self.assertTrue(start + size <= window_start or start >= window_end, name)

    def test_no_key_or_certificate_file_is_stored_under_zephyr_boot(self):
        suffixes = {".pem", ".key", ".der", ".crt", ".p12", ".pfx"}
        stored = [p for p in (REPO / "zephyr/boot").rglob("*") if p.suffix.lower() in suffixes]
        self.assertEqual([], stored)
        for path in (REPO / "zephyr/boot").rglob("*"):
            if path.is_file():
                self.assertNotIn("PRIVATE KEY", read(path), str(path))


class PageTest(unittest.TestCase):
    def test_the_labels_are_the_ones_the_page_defines(self):
        text = read(PAGE)
        self.assertIn(
            "Statements carry the labels **Verified by build**, **Verified on source** "
            "(read in the named Zephyr source tree, as in the boot and update investigation), "
            "**Inference** and **Not verified**.",
            text,
        )
        self.assertNotIn("same labels as there", text)
        self.assertNotIn("Verified by command", text)
        # Only the defined labels, in the page and in the file headers.
        defined = ("Verified by build", "Verified on source", "Verified:")
        for path in [PAGE] + [BOARD_DIR / n for n in FILES]:
            body = read(path)
            for label in re.findall(r"Verified\s+(?:by|on)\s+\w+|Unverified", body):
                self.assertTrue(label.startswith(defined[:2]), f"{path.name}: {label}")

    def test_the_page_and_the_files_say_that_the_ci_step_exists_but_nothing_is_verified_by_build(self):
        page = read(PAGE)
        self.assertNotIn("no CI step exists", page)
        self.assertNotIn("No CI step runs the script", page)
        self.assertIn("runtime shard", page)
        self.assertIn("may fail on the memory map", page)
        self.assertIn("until the first run passes", page)
        for text in (read(SCRIPT), *(read(BOARD_DIR / n) for n in FILES)):
            self.assertNotIn("No CI step runs it yet", text)
            self.assertNotIn("Nothing was built or run for this board", text)

    def test_the_open_points_record_the_owner_choices_and_the_limit_of_the_guard(self):
        rows = {l.split("|")[1].strip(): l for l in read(PAGE).splitlines() if l.startswith("| D")}
        self.assertIn("the owner chose the second script", rows["D4"])
        self.assertNotIn("is the owner's decision", rows["D4"])
        self.assertIn("skips in CI", rows["D3"])
        self.assertIn("local runs only", rows["D3"])
        self.assertNotIn("as proposed in the design note", read(PAGE))

    def test_the_padded_slot_clause_has_its_own_label(self):
        row = next(l for l in read(PAGE).splitlines() if l.startswith("| Slots"))
        self.assertIn("**Inference**", row)
        self.assertIn("padded image", row)

    APPROVAL = "approved as recommended by the project owner on 15 November 2026"

    def test_the_page_says_the_starting_values_were_approved_and_are_not_verified_by_build(self):
        text = read(PAGE)
        status = next(l for l in text.splitlines() if l.startswith("> **Status:**"))
        self.assertIn(self.APPROVAL, status)
        self.assertIn("starting values", status.lower())
        self.assertIn("corrected by the first CI build", status)
        self.assertIn("**Not verified**", status)
        self.assertNotIn("Nothing here is adopted", text)
        self.assertNotIn("has not accepted", text)

    def test_the_headers_say_the_starting_values_were_approved(self):
        for path in [SCRIPT] + [BOARD_DIR / n for n in FILES]:
            text = " ".join(l.lstrip("#/* ").rstrip() for l in read(path).splitlines()[:12])
            self.assertIn(self.APPROVAL, text, path.name)
            self.assertNotIn("not adopted", text, path.name)
            self.assertNotIn("PROPOSED", text, path.name)

    def test_the_argument_set_names_files_that_exist(self):
        named = set(re.findall(r"zephyr/boot/boards/%s/([\w.]+)" % re.escape(BOARD), read(PAGE)))
        self.assertEqual(set(FILES), named)
        for name in named:
            self.assertTrue((BOARD_DIR / name).is_file(), name)


class BuildCheckTest(unittest.TestCase):
    def setUp(self):
        self.script = read(SCRIPT)
        # Comments may name what the script avoids, so the rules that forbid a word read code only.
        self.code = "\n".join(l for l in self.script.splitlines() if not l.lstrip().startswith("#"))

    def test_the_script_is_executable(self):
        self.assertTrue(os.access(SCRIPT, os.X_OK))

    def test_the_script_targets_the_rt1170_files_and_build_directory(self):
        self.assertIn("'mimxrt1170_evk@A//cm7'", self.code)
        self.assertIn("zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A", self.code)
        self.assertIn("build/zephyr/boot-composition-rt1170", self.code)

    def test_the_script_builds_the_published_argument_set(self):
        for argument in (
            "--sysbuild",
            "SB_CONFIG_BOOTLOADER_MCUBOOT=y",
            "SB_CONFIG_MCUBOOT_MODE_RAM_LOAD_WITH_REVERT=y",
            "SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y",
            "mcuboot_EXTRA_CONF_FILE=",
            "mcuboot_EXTRA_DTC_OVERLAY_FILE=",
            "_EXTRA_CONF_FILE=",
            "_EXTRA_DTC_OVERLAY_FILE=",
        ):
            self.assertIn(argument, self.code)

    def test_the_script_checks_the_window_it_configures(self):
        self.assertIn(f"CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START={SDRAM_START:#x}", self.code)
        self.assertIn(f"CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE={SDRAM_SIZE}", self.code)

    def test_the_script_has_no_key_argument_and_no_module_argument(self):
        # No key argument and no -DEXTRA_ZEPHYR_MODULES, as in the RT1060 recipe.
        self.assertNotIn("SB_CONFIG_BOOT_SIGNATURE_KEY_FILE", self.code)
        self.assertNotIn("-DEXTRA_ZEPHYR_MODULES", self.code)
        self.assertIn("+crux-boot", self.code)

    def test_the_script_never_programs_or_generates_a_key(self):
        for forbidden in ("imgtool.py keygen", " keygen", "west flash", "west debug", "openocd", "pyocd", "JLink"):
            self.assertNotIn(forbidden, self.code)

    def test_the_script_signs_and_verifies_with_the_public_test_key(self):
        self.assertIn("root-ec-p256.pem", self.code)
        self.assertIn("verify", self.code)
        self.assertIn("Traceback", self.code)
        self.assertIn("root-rsa-2048.pem", self.code)

    def test_the_script_checks_that_modified_images_are_refused(self):
        self.assertIn("expect_refused", self.code)
        self.assertIn("0x300 0x500", self.code)

    def test_the_application_configuration_is_read_from_its_own_build_directory(self):
        self.assertIn('app_config="$build/$image/zephyr/.config"', self.code)
        self.assertNotIn("-name .config", self.code)

    def test_the_application_directory_comes_from_the_environment_with_the_controller_sample_as_default(self):
        # Open point D2: the controller sample has no RT1170 board configuration.
        self.assertIn("CRUX_BOOT_RT1170_APP:-$repo/samples/controller/zephyr", self.code)
        self.assertIn("CRUX_BOOT_RT1170_APP", self.script.split("set -euo pipefail")[0])

    def test_a_relative_application_directory_is_made_absolute_after_the_existence_check(self):
        # `west build` runs after `cd "$workspace"`, and a value of `.` would name the image `.`.
        lines = self.code.splitlines()
        exists = next(i for i, l in enumerate(lines) if '[[ -d "$app" ]]' in l)
        absolute = next(i for i, l in enumerate(lines) if 'app="$(CDPATH= cd -- "$app" && pwd)"' in l)
        named = next(i for i, l in enumerate(lines) if 'image="$(basename "$app")"' in l)
        self.assertLess(exists, absolute)
        self.assertLess(absolute, named)

    def test_the_application_path_resolution_ignores_cdpath(self):
        # With CDPATH set, a bare `cd` may print the directory and resolve it elsewhere.
        self.assertIn('app="$(CDPATH= cd -- "$app" && pwd)"', self.code)

    def test_the_application_path_resolution_gives_an_absolute_path_with_cdpath_set(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "other" / "app").mkdir(parents=True)
            (Path(tmp) / "cwd" / "app").mkdir(parents=True)
            line = next(l for l in self.code.splitlines() if 'app="$(CDPATH= cd' in l).strip()
            result = subprocess.run(
                ["bash", "-c", f'{line}; printf %s "$app"'],
                cwd=Path(tmp) / "cwd", env={**os.environ, "app": "app", "CDPATH": str(Path(tmp) / "other")},
                capture_output=True, text=True, check=True,
            )
            self.assertEqual(str((Path(tmp) / "cwd" / "app").resolve()), str(Path(result.stdout).resolve()))
            self.assertTrue(result.stdout.startswith("/"))

    def test_the_script_is_linux_only(self):
        self.assertIn('uname -s', self.code)
        self.assertIn("exit 2", self.code)


def workflow_steps(text):
    """Return the steps of the workflow as (name, [field lines]) in order, comments dropped."""
    steps = []
    for line in text.splitlines():
        if line.startswith("      - name: "):
            steps.append((line[len("      - name: "):], []))
        elif steps and line.startswith("        ") and not line.lstrip().startswith("#"):
            steps[-1][1].append(line)
    return steps


RT1060_STEP = "Build, sign and check the MCUboot RAM load composition (software-only)"


class WorkflowTest(unittest.TestCase):
    """The CI step was approved by the owner (open point D3, "all as recommended", 15 November 2026)."""

    def setUp(self):
        self.text = read(ZEPHYR_WORKFLOW)
        self.steps = workflow_steps(self.text)
        self.names = [name for name, _ in self.steps]

    def test_the_rt1170_check_runs_in_its_own_runtime_step(self):
        matches = [(n, f) for n, f in self.steps if "scripts/test-boot-composition-rt1170.sh" in "\n".join(f)]
        self.assertEqual(1, len(matches), "exactly one step runs the RT1170 check")
        name, fields = matches[0]
        self.assertIn("RT1170", name)
        self.assertEqual(
            [
                "        if: matrix.shard == 'runtime'",
                "        working-directory: crux-pilot",
                "        run: scripts/test-boot-composition-rt1170.sh",
            ],
            fields,
        )

    def test_the_rt1170_step_follows_the_rt1060_step_and_precedes_the_twister_evidence(self):
        # The workflow has another step with RT1170 in its name, the cross-build, which runs earlier.
        rt1170 = next(n for n in self.names if "RT1170" in n and "MCUboot RAM load" in n)
        self.assertLess(self.names.index(RT1060_STEP), self.names.index(rt1170))
        self.assertLess(self.names.index(rt1170), self.names.index("Collect Twister evidence"))
        # Nothing sits between the two boot composition steps.
        self.assertEqual(self.names.index(RT1060_STEP) + 1, self.names.index(rt1170))

    def test_the_rt1060_step_is_unchanged(self):
        fields = dict(self.steps)[RT1060_STEP]
        self.assertEqual(
            [
                "        if: matrix.shard == 'runtime'",
                "        working-directory: crux-pilot",
                "        run: scripts/test-boot-composition.sh",
            ],
            fields,
        )

    def test_the_comment_of_the_rt1170_step_is_accurate_about_uploads(self):
        # The existing upload globs also match build/zephyr/boot-composition-rt1170, so its
        # top-level logs and configuration are uploaded. The images are two levels down.
        comment = " ".join(
            l.strip().lstrip("# ") for l in self.text.splitlines() if l.strip().startswith("#")
        )
        self.assertNotIn("no output uploaded", comment)
        self.assertIn("No image or signed binary is uploaded", comment)
        self.assertIn("top-level sysbuild logs and configuration are", comment)
        self.assertIn("through the existing upload globs", comment)

    def test_the_change_to_the_workflow_only_adds_lines(self):
        # Every line of the workflow at the point where this branch left origin/main is still
        # present, in the same order. This guards the additive CI change of this branch. It is
        # skipped when git or origin/main is not available, as in a shallow checkout.
        try:
            base = subprocess.run(
                ["git", "merge-base", "origin/main", "HEAD"], cwd=REPO, capture_output=True, text=True, check=True
            ).stdout.strip()
            old = subprocess.run(
                ["git", "show", f"{base}:.github/workflows/zephyr.yml"],
                cwd=REPO, capture_output=True, text=True, check=True,
            ).stdout
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("origin/main is not available")
        self.assertTrue(old.strip(), "the old workflow is empty")
        new_lines = iter(self.text.splitlines())
        for line in old.splitlines():
            self.assertTrue(any(line == candidate for candidate in new_lines), f"removed or reordered: {line!r}")


if __name__ == "__main__":
    unittest.main()
