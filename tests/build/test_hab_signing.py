#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Sign a synthetic image with throwaway HAB keys and check its structure. Software only.

For each run this test generates a fresh PKI tree (RSA-3072, four SRKs) with the pinned SPSDK
`nxpcrypto pki-tree hab`, exports the SRK table, signs a synthetic application image with
`nxpimage hab export` for the NOR layout and for the SD/eMMC layout, and runs
tests/target/check_hab_structure.py on the result. Everything happens in one temporary directory
outside the repository that is deleted afterwards. The keys are never printed, never copied into
the repository, never reused and never programmed anywhere. No board, fuse, SRK hash write or closed
configuration is involved, and no vendor image is used: the input is built in memory below.

What a pass shows: the pinned SPSDK accepts a plain binary for the RT1060 family, writes a HAB container
whose IVT, boot data, DCD pointer and CSF pointer the structure checker reads as consistent with the
declaration, and the checker rejects the controls (unsigned image declared signed, zeroed CSF pointer).
What it does NOT show: that any signature is valid (the checker verifies none, and `nxpimage hab verify`
of SPSDK 3.13.0 accepted a copy with a changed byte in a manual trial, so neither is a signature check),
that the boot ROM accepts the image, or anything about a board. Whether SPSDK accepts a Zephyr-built image
that already has an FCB and an IVT is not tested here: the input is a plain binary.

The test needs `nxpcrypto` and `nxpimage` from SPSDK 3.13.0 on PATH (see tests/target/hab-requirements.txt).
Without them it is skipped, unless CRUX_REQUIRE_SPSDK=1 is set, as the CI workflow does, in which case a
missing tool fails the test.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CHECKER = REPO / "tests/target/check_hab_structure.py"
FAMILY = "mimxrt1060"
PINNED_VERSION = "3.13.0"
KEY_TYPE = "rsa3072"
REQUIRED = os.environ.get("CRUX_REQUIRE_SPSDK") == "1"

# Layouts as the SPSDK template and the repository's records give them (docs/boot-hab-status.md).
LAYOUTS = {
    "nor": {"start": 0x60000000, "ivt": 0x1000, "load": 0x2000, "device": "flexspi_nor"},
    "emmc": {"start": 0x80000000, "ivt": 0x400, "load": 0x1000, "device": "mmc"},
}


def load_checker():
    spec = importlib.util.spec_from_file_location("check_hab_structure_signing", CHECKER)
    module = importlib.util.module_from_spec(spec)
    sys.modules["check_hab_structure_signing"] = module
    spec.loader.exec_module(module)
    return module


def run(*argv, cwd):
    result = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            timeout=300)
    output = result.stdout
    if "PRIVATE KEY" in output:  # never let key material reach a log or an assertion message
        output = "[output withheld: it contained private key material]"
    return result.returncode, output


def application(start, size=0x2000):
    """A synthetic application: a vector table (initial stack, reset vector) and filler."""
    data = bytearray(b"\xa5" * size)
    struct.pack_into("<II", data, 0, 0x20010000, start + 0x2001)
    return bytes(data)


@unittest.skipUnless(shutil.which("nxpimage") and shutil.which("nxpcrypto") or REQUIRED,
                     "SPSDK (nxpimage, nxpcrypto) is not installed; see tests/target/hab-requirements.txt")
class ThrowawayHabSigning(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.nxpimage = shutil.which("nxpimage")
        cls.nxpcrypto = shutil.which("nxpcrypto")
        if not (cls.nxpimage and cls.nxpcrypto):
            raise AssertionError("CRUX_REQUIRE_SPSDK=1 but nxpimage or nxpcrypto is not on PATH")
        cls.checker = load_checker()
        cls._tmp = tempfile.TemporaryDirectory(prefix="crux-hab-throwaway-")
        cls.addClassCleanup(cls._tmp.cleanup)
        cls.work = Path(cls._tmp.name).resolve()
        if REPO in cls.work.parents or cls.work == REPO:
            raise AssertionError("the throwaway key directory must be outside the repository")
        code, out = run(cls.nxpimage, "--version", cwd=cls.work)
        if PINNED_VERSION not in out:
            raise AssertionError(f"expected SPSDK {PINNED_VERSION}, got: {out.strip()[:200]}")
        # Fresh throwaway keys: never reused, never printed (the output lists file paths only).
        code, out = run(cls.nxpcrypto, "pki-tree", "hab", "-k", KEY_TYPE, "-o", "pki", "-ca", "-e", "pem",
                        cwd=cls.work)
        if code != 0:
            raise AssertionError("PKI tree generation failed:\n" + out[-1500:])
        srk_certs = sorted((cls.work / "pki" / "crts").glob(f"SRK*_{KEY_TYPE}_ca_cert.pem"))
        if len(srk_certs) != 4:
            raise AssertionError(f"expected four SRK certificates, found {len(srk_certs)}")
        args = [cls.nxpcrypto, "rot", "export", "-f", FAMILY, "-o", "srk_table.bin"]
        for cert in srk_certs:
            args += ["-k", str(cert)]
        code, out = run(*args, cwd=cls.work)
        if code != 0 or not (cls.work / "srk_table.bin").is_file():
            raise AssertionError("SRK table export failed:\n" + out[-1500:])

    def export(self, name, medium, signed):
        layout = LAYOUTS[medium]
        (self.work / f"{name}.app.bin").write_bytes(application(layout["start"]))
        lines = [
            "options:",
            f"  flags: {8 if signed else 0}",
            f"  startAddress: {layout['start']:#x}",
            f"  ivtOffset: {layout['ivt']:#x}",
            f"  initialLoadSize: {layout['load']:#x}",
            f"  entryPointAddress: {layout['start'] + 0x2001:#x}",
            f"  family: {FAMILY}",
            "  signatureTimestamp: 01/01/2026 00:00:00",
            f"inputImageFile: {name}.app.bin",
            "output_format: bin",
        ]
        if signed:
            lines += [
                "sections:",
                "  - Header:",
                "      Header_Version: '4.2'",
                "      Header_HashAlgorithm: sha256",
                "      Header_Engine: ANY",
                "      Header_EngineConfiguration: 0",
                "      Header_CertificateFormat: x509",
                "      Header_SignatureFormat: CMS",
                "  - InstallSRK:",
                "      InstallSRK_Table: srk_table.bin",
                "      InstallSRK_SourceIndex: 0",
                "  - InstallCSFK:",
                f"      InstallCSFK_File: pki/crts/CSF0_0_{KEY_TYPE}_cert.pem",
                "      InstallCSFK_CertificateFormat: x509",
                "  - AuthenticateCSF:",
                "      AuthenticateCSF_EngineConfiguration: 0",
                "      AuthenticateCSF_CertificateFormat: x509",
                "      AuthenticateCSF_SignatureFormat: CMS",
                f"      Signer: type=file;file_path=pki/keys/CSF0_0_{KEY_TYPE}_key.pem",
                "  - InstallKey:",
                f"      InstallKey_File: pki/crts/IMG0_0_{KEY_TYPE}_cert.pem",
                "      InstallKey_VerificationIndex: 0",
                "      InstallKey_TargetIndex: 2",
                "  - AuthenticateData:",
                "      AuthenticateData_VerificationIndex: 2",
                "      AuthenticateData_Engine: ANY",
                "      AuthenticateData_EngineConfiguration: 0",
                f"      Signer: type=file;file_path=pki/keys/IMG0_0_{KEY_TYPE}_key.pem",
            ]
        else:
            lines += ["sections: []"]
        (self.work / f"{name}.yaml").write_text("\n".join(lines) + "\n")
        code, out = run(self.nxpimage, "hab", "export", "-c", f"{name}.yaml", "-o", f"{name}.container.bin",
                        cwd=self.work)
        self.assertEqual(0, code, "nxpimage hab export failed:\n" + out[-1500:])
        return (self.work / f"{name}.container.bin").read_bytes()

    @staticmethod
    def device_image(medium, container):
        """The container placed at the IVT offset behind a prefix, as a device image holds it."""
        prefix = bytearray(LAYOUTS[medium]["ivt"])
        if medium == "nor":
            prefix[0:4] = b"FCFB"  # the FlexSPI configuration block tag the checker looks for
        return bytes(prefix) + container

    def check(self, data, medium, csf, layout):
        return self.checker.check_hab_structure(data, medium, csf, dcd="absent", layout=layout)

    def test_signed_container_and_device_image_are_consistent(self):
        for medium in LAYOUTS:
            with self.subTest(medium=medium):
                container = self.export(f"signed-{medium}", medium, signed=True)
                report = self.check(container, medium, "present", "container")
                self.assertTrue(report["ok"], report)
                self.assertTrue(report["csf"]["present"])
                self.assertEqual(0, report["ivt"]["offset"])
                image = self.device_image(medium, container)
                report = self.check(image, medium, "present", "image")
                self.assertTrue(report["ok"], report)
                self.assertEqual(LAYOUTS[medium]["ivt"], report["ivt"]["offset"])
                self.assertIs(False, report["signature_verified"])

    def test_spsdk_verify_accepts_the_signed_container(self):
        container = self.export("verify-nor", "nor", signed=True)
        path = self.work / "verify-nor.container.bin"
        self.assertEqual(container, path.read_bytes())
        code, out = run(self.nxpimage, "hab", "verify", "-b", str(path), "-f", FAMILY, cwd=self.work)
        self.assertEqual(0, code, out[-1500:])
        self.assertIn("Succeeded", out)

    def test_unsigned_image_declared_signed_is_rejected(self):
        for medium in LAYOUTS:
            with self.subTest(medium=medium):
                container = self.export(f"plain-{medium}", medium, signed=False)
                self.assertTrue(self.check(container, medium, "absent", "container")["ok"])
                report = self.check(container, medium, "present", "container")
                self.assertFalse(report["ok"])
                self.assertEqual("structure inconsistent", report["verdict"])

    def test_zeroed_csf_pointer_is_rejected(self):
        container = bytearray(self.export("zeroed-nor", "nor", signed=True))
        struct.pack_into("<I", container, 4 + 4 * 5, 0)  # the csf word of the IVT at file offset 0
        self.assertFalse(self.check(bytes(container), "nor", "present", "container")["ok"])
        self.assertTrue(self.check(bytes(container), "nor", "absent", "container")["ok"])

    def test_changed_payload_byte_is_not_a_structure_error(self):
        # The checker is not a signature check, and this test records that: a changed payload byte
        # leaves the structure consistent.
        container = bytearray(self.export("tampered-nor", "nor", signed=True))
        container[0x1800] ^= 0xFF
        report = self.check(bytes(container), "nor", "present", "container")
        self.assertTrue(report["ok"], report)
        self.assertIs(False, report["signature_verified"])

    def test_keys_stay_in_the_temporary_directory(self):
        self.assertTrue(any(self.work.rglob("*_key.pem")))
        self.assertEqual([], list(REPO.rglob("*_key.pem")))
        self.assertEqual([], list(REPO.rglob("*_cert.pem")))
        self.assertEqual([], list(REPO.rglob("srk_table.bin")))


if __name__ == "__main__":
    unittest.main()
