#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check tests/target/check_hab_structure.py on synthetic boot images.

Every input is built in memory from this file's own reading of the HAB boot-data structures. The
checker shares that reading, so these tests cannot find an error that the reading itself contains:
the IVT, boot data, DCD and CSF header layouts are the author's reading of public descriptions and
the repository's own records (docs/boot-hab-status.md says which statement carries which label). No
input here is an image from a board, from a build or from a signing tool, and none contains a key, a
certificate or a real signature. Nothing here checks a signature.
The tests use the standard library only, like the tool.
"""
import ast
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "tests/target/check_hab_structure.py"

IVT_SIZE = 32
BOOT_DATA_SIZE = 12
IVT_OFFSET = {"nor": 0x1000, "sd": 0x400, "emmc": 0x400}
BASE = {"nor": 0x60000000, "sd": 0x80000000, "emmc": 0x80000000}
FCB_SIZE = 512
FCB_TAG = b"FCFB"


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def header(tag, length, version):
    """A HAB structure header: tag, 16-bit big-endian length, version."""
    return bytes([tag]) + struct.pack(">H", length) + bytes([version])


def build_image(medium="nor", csf=False, dcd=True, size=0x4000, csf_len=0x80, dcd_len=16, **override):
    """A synthetic image. override replaces any named field after the default layout is built.

    Fields: ivt_tag, ivt_len, ivt_version, entry, reserved1, dcd_ptr, boot_data_ptr, self_ptr,
    csf_ptr, reserved2, bd_start, bd_length, bd_plugin, dcd_tag, dcd_length, csf_tag, csf_length,
    fcb (bytes or None), ivt_offset, file_size.
    """
    ivt_offset = override.get("ivt_offset", IVT_OFFSET[medium])
    base = BASE[medium]
    self_addr = base + ivt_offset
    boot_data_addr = self_addr + IVT_SIZE
    dcd_addr = boot_data_addr + 0x10  # Zephyr RT10xx NOR layout: IVT 0x1000, boot data 0x1020, DCD 0x1030
    csf_off = 0x3000
    csf_addr = base + csf_off
    fields = {
        "ivt_tag": 0xD1, "ivt_len": IVT_SIZE, "ivt_version": 0x41,
        "entry": base + 0x2000, "reserved1": 0,
        "dcd_ptr": dcd_addr if dcd else 0,
        "boot_data_ptr": boot_data_addr,
        "self_ptr": self_addr,
        "csf_ptr": csf_addr if csf else 0,
        "reserved2": 0,
        "bd_start": base, "bd_length": size, "bd_plugin": 0,
        "dcd_tag": 0xD2, "dcd_length": dcd_len,
        "csf_tag": 0xD4, "csf_length": csf_len,
        "fcb": FCB_TAG + bytes(FCB_SIZE - len(FCB_TAG)) if medium == "nor" else None,
    }
    fields.update(override)
    image = bytearray(b"\xff" * fields.get("file_size", size))
    if fields["fcb"] is not None:
        image[0:len(fields["fcb"])] = fields["fcb"]
    elif medium != "nor":
        image[0:16] = bytes(16)
    ivt = header(fields["ivt_tag"], fields["ivt_len"], fields["ivt_version"]) + struct.pack(
        "<IIIIIII", fields["entry"], fields["reserved1"], fields["dcd_ptr"], fields["boot_data_ptr"],
        fields["self_ptr"], fields["csf_ptr"], fields["reserved2"])
    image[ivt_offset:ivt_offset + IVT_SIZE] = ivt
    bd_off = ivt_offset + IVT_SIZE
    image[bd_off:bd_off + BOOT_DATA_SIZE] = struct.pack(
        "<III", fields["bd_start"], fields["bd_length"], fields["bd_plugin"])
    if dcd:
        d_off = bd_off + 0x10
        body = bytes(max(0, fields["dcd_length"] - 4))
        image[d_off:d_off + 4] = header(fields["dcd_tag"], fields["dcd_length"], 0x41)
        image[d_off + 4:d_off + 4 + len(body)] = body
    if csf:
        image[csf_off:csf_off + 4] = header(fields["csf_tag"], fields["csf_length"], 0x45)
        body = bytes(max(0, fields["csf_length"] - 4))
        image[csf_off + 4:csf_off + 4 + len(body)] = body
    return bytes(image[:fields.get("file_size", size)])


class TempFiles(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)

    def write(self, name, data):
        path = self.tmp / name
        path.write_bytes(data)
        return str(path)


class ScriptExists(unittest.TestCase):
    def test_script_exists_and_uses_the_standard_library_only(self):
        self.assertTrue(SCRIPT.is_file(), SCRIPT)
        tree = ast.parse(SCRIPT.read_text())
        names = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names.update(alias.name.split(".")[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                names.add(node.module.split(".")[0])
        stdlib = getattr(sys, "stdlib_module_names", None)
        if stdlib is None:
            self.skipTest("sys.stdlib_module_names needs Python 3.10 or later")
        self.assertEqual(set(), names - set(stdlib))

    def test_inspector_is_not_a_dependency(self):
        text = SCRIPT.read_text()
        self.assertNotIn("import inspect_boot_image", text)
        self.assertNotIn("from inspect_boot_image", text)


class Base(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = load_module(SCRIPT, "check_hab_structure_under_test")

    def check(self, data, medium="nor", csf="absent", dcd=None):
        return self.mod.check_hab_structure(data, medium, csf, dcd=dcd)

    def status(self, report, name):
        found = [entry for entry in report["checks"] if entry["name"] == name]
        self.assertEqual(1, len(found), f"{name}: {[e['name'] for e in report['checks']]}")
        return found[0]["status"]

    def assertFails(self, report, name):
        self.assertEqual("fail", self.status(report, name), json.dumps(report, indent=1))
        self.assertFalse(report["ok"])
        self.assertEqual("structure inconsistent", report["verdict"])


class ConsistentImages(Base):
    def test_nor_without_csf(self):
        report = self.check(build_image("nor"), "nor", "absent")
        self.assertTrue(report["ok"], json.dumps(report, indent=1))
        self.assertEqual("structure consistent", report["verdict"])
        self.assertEqual(0x1000, report["ivt"]["offset"])
        self.assertEqual("absent", report["csf"]["declared"])
        self.assertFalse(report["csf"]["present"])

    def test_nor_with_csf(self):
        report = self.check(build_image("nor", csf=True), "nor", "present")
        self.assertTrue(report["ok"], json.dumps(report, indent=1))
        self.assertTrue(report["csf"]["present"])
        self.assertEqual(0x3000, report["csf"]["offset"])
        self.assertEqual(0x80, report["csf"]["length"])

    def test_sd_and_emmc_use_0x400(self):
        for medium in ("sd", "emmc"):
            for csf in (False, True):
                with self.subTest(medium=medium, csf=csf):
                    report = self.check(build_image(medium, csf=csf), medium, "present" if csf else "absent")
                    self.assertTrue(report["ok"], json.dumps(report, indent=1))
                    self.assertEqual(0x400, report["ivt"]["offset"])

    def test_report_fields(self):
        report = self.check(build_image("nor", csf=True), "nor", "present")
        self.assertEqual(0x1000 + 0x20, report["boot_data"]["offset"])
        self.assertEqual(0x60000000, report["boot_data"]["start"])
        self.assertEqual(0x4000, report["boot_data"]["length"])
        self.assertEqual(0, report["boot_data"]["plugin"])
        self.assertTrue(report["dcd"]["present"])
        self.assertEqual(16, report["dcd"]["length"])
        self.assertEqual(0x1030, report["dcd"]["offset"])

    def test_signature_is_never_claimed(self):
        report = self.check(build_image("nor", csf=True), "nor", "present")
        self.assertIs(False, report["signature_verified"])
        self.assertIn("NOT a signature verification", report["signature_note"])


class IvtOffset(Base):
    def test_sd_layout_checked_as_nor_fails_with_hint(self):
        report = self.check(build_image("sd"), "nor", "absent")
        self.assertFails(report, "ivt")
        detail = [e for e in report["checks"] if e["name"] == "ivt"][0]["detail"]
        self.assertIn("0x400", detail)
        self.assertIn("0x1000", detail)

    def test_nor_layout_checked_as_emmc_fails_with_hint(self):
        report = self.check(build_image("nor"), "emmc", "absent")
        self.assertFails(report, "ivt")
        detail = [e for e in report["checks"] if e["name"] == "ivt"][0]["detail"]
        self.assertIn("0x1000", detail)

    def test_no_ivt_anywhere(self):
        report = self.check(b"\xff" * 0x4000, "nor", "absent")
        self.assertFails(report, "ivt")

    def test_empty_and_short_files_do_not_crash(self):
        for data in (b"", b"\xd1", bytes(0x1010)):
            with self.subTest(size=len(data)):
                report = self.check(data, "nor", "absent")
                self.assertFalse(report["ok"])
                self.assertEqual("structure inconsistent", report["verdict"])

    def test_unknown_medium_is_a_value_error(self):
        with self.assertRaises(ValueError):
            self.mod.check_hab_structure(build_image("nor"), "nand", "absent")

    def test_unknown_csf_declaration_is_a_value_error(self):
        with self.assertRaises(ValueError):
            self.mod.check_hab_structure(build_image("nor"), "nor", "maybe")


class IvtHeader(Base):
    def test_bad_tag(self):
        data = build_image("nor", ivt_tag=0xD2)
        self.assertFails(self.check(data), "ivt")

    def test_bad_length(self):
        self.assertFails(self.check(build_image("nor", ivt_len=0x30)), "ivt_header")

    def test_bad_version(self):
        self.assertFails(self.check(build_image("nor", ivt_version=0x10)), "ivt_header")

    def test_both_known_versions_pass(self):
        for version in (0x40, 0x41):
            with self.subTest(version=version):
                self.assertTrue(self.check(build_image("nor", ivt_version=version))["ok"])

    def test_zero_entry_is_a_warning_not_a_failure(self):
        report = self.check(build_image("nor", entry=0))
        self.assertTrue(report["ok"])
        self.assertEqual("warn", self.status(report, "entry"))

    def test_entry_outside_the_image_range_warns(self):
        report = self.check(build_image("nor", entry=0x10000000))
        self.assertTrue(report["ok"])
        self.assertEqual("warn", self.status(report, "entry_range"))

    def test_reserved_words_nonzero_warn(self):
        report = self.check(build_image("nor", reserved1=1, reserved2=2))
        self.assertTrue(report["ok"])
        self.assertEqual("warn", self.status(report, "reserved"))


class BootData(Base):
    def test_pointer_zero(self):
        self.assertFails(self.check(build_image("nor", boot_data_ptr=0)), "boot_data")

    def test_pointer_below_image_base(self):
        self.assertFails(self.check(build_image("nor", boot_data_ptr=0x5FFF0000)), "boot_data")

    def test_pointer_beyond_file(self):
        self.assertFails(self.check(build_image("nor", boot_data_ptr=0x60000000 + 0x4000 - 4)), "boot_data")

    def test_self_pointer_must_match_the_ivt_position(self):
        self.assertFails(self.check(build_image("nor", self_ptr=0x60002000)), "self")

    def test_start_must_match_image_base(self):
        # The image base is self minus the IVT offset; boot data start says another base. The two
        # fields disagree, and the tool reports that under the self check.
        self.assertFails(self.check(build_image("nor", bd_start=0x60001000)), "self")

    def test_length_zero(self):
        self.assertFails(self.check(build_image("nor", bd_length=0)), "boot_data")

    def test_length_beyond_file(self):
        self.assertFails(self.check(build_image("nor", bd_length=0x8000)), "boot_data")

    def test_length_that_does_not_cover_the_boot_header(self):
        self.assertFails(self.check(build_image("nor", bd_length=0x1000)), "boot_data")

    def test_length_shorter_than_file_is_a_note(self):
        report = self.check(build_image("nor", bd_length=0x3000))
        self.assertTrue(report["ok"], json.dumps(report, indent=1))
        self.assertEqual("info", self.status(report, "image_length"))

    def test_odd_plugin_flag_warns(self):
        report = self.check(build_image("nor", bd_plugin=7))
        self.assertTrue(report["ok"])
        self.assertEqual("warn", self.status(report, "plugin"))


class Dcd(Base):
    def test_declared_absent_and_absent(self):
        report = self.check(build_image("nor", dcd=False), dcd="absent")
        self.assertTrue(report["ok"], json.dumps(report, indent=1))
        self.assertFalse(report["dcd"]["present"])

    def test_declared_absent_but_present(self):
        self.assertFails(self.check(build_image("nor"), dcd="absent"), "dcd")

    def test_declared_present_but_absent(self):
        self.assertFails(self.check(build_image("nor", dcd=False), dcd="present"), "dcd")

    def test_not_declared_reports_what_it_finds(self):
        self.assertTrue(self.check(build_image("nor", dcd=False))["ok"])
        self.assertTrue(self.check(build_image("nor"))["ok"])

    def test_bad_tag(self):
        self.assertFails(self.check(build_image("nor", dcd_tag=0xD4)), "dcd")

    def test_length_over_the_documented_maximum(self):
        self.assertFails(self.check(build_image("nor", dcd_length=1769)), "dcd")

    def test_length_at_the_documented_maximum_passes(self):
        self.assertTrue(self.check(build_image("nor", dcd_length=1768))["ok"])

    def test_length_shorter_than_its_header(self):
        self.assertFails(self.check(build_image("nor", dcd_length=3)), "dcd")

    def test_pointer_outside_the_file(self):
        self.assertFails(self.check(build_image("nor", dcd_ptr=0x60000000 + 0x8000)), "dcd")

    def test_length_beyond_the_file(self):
        image = build_image("nor", dcd_length=1768, file_size=0x1040 + 100, bd_length=0x1040 + 100)
        self.assertFails(self.check(image), "dcd")

    def test_content_is_not_parsed(self):
        report = self.check(build_image("nor"))
        self.assertIn("commands are not parsed", report["dcd"]["note"])


class CsfDeclaration(Base):
    def test_declared_present_but_pointer_zero(self):
        self.assertFails(self.check(build_image("nor"), csf="present"), "csf")

    def test_declared_absent_but_pointer_set(self):
        self.assertFails(self.check(build_image("nor", csf=True), csf="absent"), "csf")

    def test_declared_present_pointer_below_base(self):
        self.assertFails(self.check(build_image("nor", csf=True, csf_ptr=0x5F000000), csf="present"), "csf")

    def test_declared_present_pointer_beyond_file(self):
        self.assertFails(self.check(build_image("nor", csf=True, csf_ptr=0x60000000 + 0x8000), csf="present"),
                         "csf")

    def test_placeholder_without_a_csf_header(self):
        # The pointer is set but the region holds the erased value: not a CSF.
        image = bytearray(build_image("nor", csf=True))
        image[0x3000:0x3004] = b"\xff\xff\xff\xff"
        report = self.check(bytes(image), csf="present")
        self.assertFails(report, "csf")
        detail = [e for e in report["checks"] if e["name"] == "csf"][0]["detail"]
        self.assertIn("no CSF header", detail)

    def test_wrong_tag(self):
        self.assertFails(self.check(build_image("nor", csf=True, csf_tag=0xD2), csf="present"), "csf")

    def test_length_runs_beyond_the_file(self):
        self.assertFails(self.check(build_image("nor", csf=True, csf_length=0x2000), csf="present"), "csf")

    def test_length_shorter_than_its_header(self):
        self.assertFails(self.check(build_image("nor", csf=True, csf_length=2), csf="present"), "csf")

    def test_csf_overlapping_the_boot_data_fails(self):
        image = build_image("nor", csf=True, csf_ptr=0x60000000 + 0x1020)
        self.assertFails(self.check(image, csf="present"), "regions")

    def test_csf_pointing_at_the_ivt_fails(self):
        image = build_image("nor", csf=True, csf_ptr=0x60001000)
        self.assertFails(self.check(image, csf="present"), "regions")

    def test_csf_outside_the_boot_data_range_warns_without_failing(self):
        report = self.check(build_image("nor", csf=True, bd_length=0x3000), csf="present")
        self.assertTrue(report["ok"], json.dumps(report, indent=1))
        self.assertEqual("warn", self.status(report, "csf_range"))

    def test_dcd_overlapping_the_boot_data_fails(self):
        image = build_image("nor", dcd_ptr=0x60000000 + 0x1020)
        self.assertFails(self.check(image), "regions")

    def test_dcd_pointing_into_the_fcb_fails(self):
        image = build_image("nor", dcd_ptr=0x60000000 + 0x100)
        self.assertFails(self.check(image), "regions")

    def test_sd_and_emmc_negative_cases(self):
        for medium in ("sd", "emmc"):
            with self.subTest(medium=medium):
                self.assertFails(self.check(build_image(medium), medium, "present"), "csf")
                self.assertFails(self.check(build_image(medium, csf=True), medium, "absent"), "csf")
                self.assertFails(self.check(build_image(medium, ivt_tag=0x00), medium), "ivt")
                self.assertFails(self.check(build_image(medium, bd_length=0x400), medium), "boot_data")

    def test_csf_content_is_not_parsed(self):
        report = self.check(build_image("nor", csf=True), csf="present")
        self.assertIn("commands, keys and signatures are not parsed", report["csf"]["note"])


class Fcb(Base):
    def test_nor_with_the_fcb_tag(self):
        self.assertEqual("ok", self.status(self.check(build_image("nor")), "fcb"))

    def test_nor_without_the_fcb_tag_warns(self):
        report = self.check(build_image("nor", fcb=bytes(16)))
        self.assertTrue(report["ok"])
        self.assertEqual("warn", self.status(report, "fcb"))

    def test_sd_and_emmc_do_not_examine_the_fcb(self):
        for medium in ("sd", "emmc"):
            self.assertEqual("info", self.status(self.check(build_image(medium), medium), "fcb"))


class ContainerLayout(Base):
    """A bare HAB container (the IVT at file offset 0), as `nxpimage hab export` writes it.

    The synthetic container is the device image from the IVT on. The pointers and the boot data
    length still count the bytes before the IVT, which are not in the file.
    """

    def container(self, medium, **kwargs):
        return build_image(medium, **kwargs)[IVT_OFFSET[medium]:]

    def check_container(self, data, medium="nor", csf="absent", dcd=None):
        return self.mod.check_hab_structure(data, medium, csf, dcd=dcd, layout="container")

    def test_consistent_for_every_medium(self):
        for medium in ("nor", "sd", "emmc"):
            for csf in (False, True):
                with self.subTest(medium=medium, csf=csf):
                    report = self.check_container(self.container(medium, csf=csf), medium,
                                                  "present" if csf else "absent")
                    self.assertTrue(report["ok"], json.dumps(report, indent=1))
                    self.assertEqual(0, report["ivt"]["offset"])
                    self.assertEqual(IVT_OFFSET[medium], report["ivt"]["device_offset"])
                    self.assertEqual("container", report["layout"])

    def test_csf_offset_is_counted_in_the_file(self):
        report = self.check_container(self.container("nor", csf=True), "nor", "present")
        self.assertEqual(0x3000 - 0x1000, report["csf"]["offset"])

    def test_device_image_is_not_a_container(self):
        self.assertFails(self.check_container(build_image("nor"), "nor", "absent"), "ivt")

    def test_container_is_not_a_device_image(self):
        self.assertFails(self.check(self.container("nor"), "nor", "absent"), "ivt")

    def test_boot_data_length_counts_the_prefix(self):
        # Longer than the whole device image fails. A length equal to the missing prefix alone does not
        # cover the IVT and boot data, even though the file itself is longer than that.
        self.assertFails(self.check_container(self.container("nor", bd_length=0x8000), "nor"), "boot_data")
        self.assertFails(self.check_container(self.container("nor", bd_length=0x1000), "nor"), "boot_data")

    def test_csf_mismatch_and_overlap_still_fail(self):
        self.assertFails(self.check_container(self.container("nor"), "nor", "present"), "csf")
        self.assertFails(self.check_container(self.container("nor", csf=True), "nor", "absent"), "csf")
        image = self.container("nor", csf=True, csf_ptr=0x60000000 + 0x1020)
        self.assertFails(self.check_container(image, "nor", "present"), "regions")

    def test_fcb_is_not_examined(self):
        report = self.check_container(self.container("nor"), "nor")
        self.assertEqual("info", self.status(report, "fcb"))

    def test_csf_pointer_beyond_the_container(self):
        image = self.container("nor", csf=True, csf_ptr=0x60000000 + 0x8000)
        self.assertFails(self.check_container(image, "nor", "present"), "csf")

    def test_unknown_layout_is_a_value_error(self):
        with self.assertRaises(ValueError):
            self.mod.check_hab_structure(build_image("nor"), "nor", "absent", layout="slot")

    def test_cli_layout_option(self):
        mod = self.mod
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "c.bin"
            path.write_bytes(self.container("emmc", csf=True))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = mod.main(["check", str(path), "--medium", "emmc", "--layout", "container", "--csf", "present"])
            self.assertEqual(0, code, out.getvalue())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(1, mod.main(["check", str(path), "--medium", "emmc", "--csf", "present"]))


class Cli(TempFiles):
    @classmethod
    def setUpClass(cls):
        cls.mod = load_module(SCRIPT, "check_hab_structure_cli")

    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = self.mod.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_consistent_image_exit_0(self):
        path = self.write("a.bin", build_image("nor", csf=True))
        code, out, _ = self.run_cli("check", path, "--medium", "nor", "--csf", "present")
        self.assertEqual(0, code, out)
        self.assertEqual("Result: structure consistent", out.strip().splitlines()[-1])
        self.assertIn("NOT a signature verification", out)

    def test_inconsistent_image_exit_1(self):
        path = self.write("a.bin", build_image("sd"))
        code, out, _ = self.run_cli("check", path, "--medium", "nor", "--csf", "absent")
        self.assertEqual(1, code, out)
        self.assertEqual("Result: structure inconsistent", out.strip().splitlines()[-1])

    def test_json_output(self):
        path = self.write("a.bin", build_image("emmc"))
        code, out, _ = self.run_cli("check", path, "--medium", "emmc", "--csf", "absent", "--json")
        self.assertEqual(0, code)
        report = json.loads(out)
        self.assertIs(False, report["signature_verified"])
        self.assertEqual("structure consistent", report["verdict"])
        self.assertEqual(0x400, report["ivt"]["offset"])

    def test_dcd_declaration_is_passed_on(self):
        path = self.write("a.bin", build_image("nor"))
        code, _, _ = self.run_cli("check", path, "--medium", "nor", "--csf", "absent", "--dcd", "absent")
        self.assertEqual(1, code)

    def test_missing_csf_declaration_is_a_usage_error(self):
        path = self.write("a.bin", build_image("nor"))
        code, _, _ = self.run_cli("check", path, "--medium", "nor")
        self.assertEqual(2, code)

    def test_unknown_medium_is_a_usage_error(self):
        path = self.write("a.bin", build_image("nor"))
        code, _, _ = self.run_cli("check", path, "--medium", "nand", "--csf", "absent")
        self.assertEqual(2, code)

    def test_unreadable_file_is_a_usage_error(self):
        code, _, err = self.run_cli("check", str(self.tmp / "missing.bin"), "--medium", "nor", "--csf", "absent")
        self.assertEqual(2, code)
        self.assertIn("cannot read", err)


if __name__ == "__main__":
    unittest.main()
