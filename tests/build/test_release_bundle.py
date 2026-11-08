# SPDX-License-Identifier: Apache-2.0
"""Tests of scripts/check-release-bundle.py and docs/schemas/release-manifest.schema.json.

The cases that check what a bundle may hold are ported from the unit tests of the public consumer
repository wonny-labs/crux-controller (tests/tooling/test_bundle.py at commit f03dc79, Apache-2.0). The
cases for the schema version, the identity algorithm, the extensions object, the absence of a default
target, the hashes-only default and the other additions of this checker are new.

There is no assembler yet, so the tests write bundles by hand. The firmware identity in them comes
from the real functions of scripts/record-build-provenance.py.
"""
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts/check-release-bundle.py"
SCHEMA = REPO / "docs/schemas/release-manifest.schema.json"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PROVENANCE = load("provenance", REPO / "scripts/record-build-provenance.py")
CHECKER = load("check_release_bundle", SCRIPT)

IMAGE = b"\x10\x20\x30\x40" * 16
CONFIG = b"CONFIG_LOG=y\n# a comment\n# CONFIG_SHELL is not set\n\nCONFIG_MAIN_STACK_SIZE=4096\n"
TARGET = "example_board@1//qspi"
LOCK = "manifest:\n  projects:\n    - name: crux-pilot\n      revision: db5adca\n"
PINS = "CONTRACT crux-pilot db5adca28bd3\nOK zephyr dccb09599635\n"
HASH_A = "a" * 64
HASH_B = "b" * 64
REQUIRED = (
    "schema_version", "identity_algorithm", "product_version", "release_tag", "sdk_version",
    "build_image", "target", "bundle_hashes", "image_sha256", "firmware_identity",
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def firmware_identity(base, image=IMAGE, config=CONFIG, target=TARGET):
    build = Path(base) / "build"
    build.mkdir(exist_ok=True)
    (build / "zephyr.bin").write_bytes(image)
    (build / ".config").write_bytes(config)
    identity = PROVENANCE.firmware_identity(build, target)
    return identity, PROVENANCE.firmware_digest(identity)


def write_manifest(bundle, manifest):
    (Path(bundle) / "release-manifest.json").write_text(json.dumps(manifest, indent=2))


def read_manifest(bundle):
    return json.loads((Path(bundle) / "release-manifest.json").read_text())


def make_bundle(base, **overrides):
    """A complete bundle of two text items. An override of None removes that manifest field."""
    base = Path(base)
    bundle = base / "bundle"
    bundle.mkdir()
    (bundle / "west.lock.yml").write_text(LOCK)
    (bundle / "pins-report.txt").write_text(PINS)
    identity, digest = firmware_identity(base)
    manifest = {
        "schema_version": 1,
        "identity_algorithm": PROVENANCE.FIRMWARE_IDENTITY_ALGORITHM,
        "product_version": "1.2.3",
        "release_tag": "product-x-1.2.3",
        "sdk_version": "1.0.1",
        "build_image": "ubuntu-24.04",
        "target": TARGET,
        "bundle_hashes": {
            "pins-report.txt": sha256(PINS.encode()),
            "west.lock.yml": sha256(LOCK.encode()),
        },
        "image_sha256": identity["image_sha256"],
        "firmware_identity": digest,
    }
    manifest.update(overrides)
    write_manifest(bundle, {key: value for key, value in manifest.items() if value is not None})
    return bundle


def put_item(bundle, name, data):
    """Write an item and list its hash, so only the content under test can be at fault."""
    path = Path(bundle) / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    manifest = read_manifest(bundle)
    manifest["bundle_hashes"][name] = sha256(data)
    write_manifest(bundle, manifest)


def run_cli(*arguments):
    return subprocess.run([sys.executable, str(SCRIPT), *arguments], capture_output=True, text=True)


class BundleTestCase(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)

    def bundle(self, **overrides):
        return make_bundle(self.base, **overrides)

    def assertRefused(self, problems, *words):
        for word in words:
            self.assertTrue(any(word in problem for problem in problems), problems)


class SchemaFile(BundleTestCase):
    def test_schema_is_marked_provisional_and_versioned(self):
        schema = json.loads(SCHEMA.read_text())
        self.assertIn("PROVISIONAL", schema["description"])
        self.assertEqual(1, schema["properties"]["schema_version"]["const"])
        self.assertFalse(schema["additionalProperties"])

    def test_schema_requires_exactly_the_generic_fields(self):
        schema = json.loads(SCHEMA.read_text())
        self.assertEqual(set(REQUIRED), set(schema["required"]))

    def test_product_fields_are_not_platform_fields(self):
        properties = json.loads(SCHEMA.read_text())["properties"]
        self.assertNotIn("security_counter", properties)
        self.assertNotIn("runner_image", properties)
        self.assertNotIn("support_period", properties)

    def test_schema_names_no_default_target(self):
        text = SCHEMA.read_text() + SCRIPT.read_text()
        self.assertNotIn('"default"', text)
        self.assertNotIn("mimxrt1060", text)

    def test_identity_algorithm_is_the_one_of_the_build_record(self):
        schema = json.loads(SCHEMA.read_text())
        listed = schema["properties"]["identity_algorithm"]["enum"]
        self.assertEqual([PROVENANCE.FIRMWARE_IDENTITY_ALGORITHM], listed)

    def test_every_keyword_of_the_schema_is_one_that_the_checker_applies(self):
        def keywords(node):
            if isinstance(node, dict):
                for key, value in node.items():
                    yield key
                    if key in ("properties",):
                        for child in value.values():
                            yield from keywords(child)
                    elif key in ("items", "additionalProperties"):
                        yield from keywords(value)
        unknown = set(keywords(json.loads(SCHEMA.read_text()))) - CHECKER.SUPPORTED_KEYWORDS
        self.assertEqual(set(), unknown)


class ManifestFields(BundleTestCase):
    def test_complete_manifest_is_accepted(self):
        self.assertEqual([], CHECKER.check(self.bundle()))

    def test_identity_of_the_build_record_is_accepted(self):
        identity, digest = firmware_identity(self.base, image=IMAGE + b"\x01")
        bundle = self.bundle(image_sha256=identity["image_sha256"], firmware_identity=digest)
        self.assertEqual([], CHECKER.check(bundle))

    def test_each_required_field_is_required(self):
        for name in REQUIRED:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as base:
                problems = CHECKER.check(make_bundle(base, **{name: None}))
                self.assertRefused(problems, name)

    def test_target_has_no_default(self):
        self.assertRefused(CHECKER.check(self.bundle(target=None)), "target")

    def test_unknown_field_is_refused(self):
        self.assertRefused(CHECKER.check(self.bundle(colour="red")), "colour")

    def test_product_fields_are_refused_at_the_top_level(self):
        for name, value in (("security_counter", 0), ("runner_image", "ubuntu-24.04"), ("support_period", "5 years")):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, **{name: value})), name)

    def test_schema_version_must_be_the_one_this_checker_knows(self):
        for value in (0, 2, "1", True):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, schema_version=value)), "schema_version")

    def test_identity_algorithm_must_be_a_known_one(self):
        for value in ("crux-firmware-identity-v2", "sha256", ""):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, identity_algorithm=value)), "identity_algorithm")

    def test_malformed_values_are_refused(self):
        cases = (
            ("firmware_identity", "not-a-hash"),
            ("image_sha256", "A" * 64),
            ("product_version", "1.2"),
            ("product_version", "v1.2.3"),
            ("release_tag", "has space"),
            ("release_tag", "../escape"),
            ("sdk_version", "1.0"),
            ("build_image", "/abs/path"),
            ("target", "bad target"),
            ("bundle_hashes", {"x": "short"}),
            ("bundle_hashes", []),
        )
        for name, value in cases:
            with self.subTest(name=name, value=value), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, **{name: value})), name)

    def test_product_version_may_carry_build_metadata_or_a_prerelease(self):
        for value in ("1.2.3+4", "1.2.3-rc.1", "1.2.3-rc.1+build.7"):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as base:
                self.assertEqual([], CHECKER.check(make_bundle(base, product_version=value)))

    def test_release_tag_is_not_tied_to_one_form(self):
        for value in ("v1.2.3", "product-x-1.2.3", "release_1.2.3"):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as base:
                self.assertEqual([], CHECKER.check(make_bundle(base, release_tag=value)))

    def test_extensions_hold_product_fields(self):
        extensions = {"security_counter": 7, "build_number": 12, "nested": {"free": ["form"]}}
        self.assertEqual([], CHECKER.check(self.bundle(extensions=extensions)))

    def test_extensions_must_be_an_object(self):
        for value in ([], "x", 3):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, extensions=value)), "extensions")

    def test_extensions_are_still_scanned_for_paths_and_private_strings(self):
        bundle = self.bundle(extensions={"note": "built in /home/runner/work/app"})
        self.assertRefused(CHECKER.check(bundle), "release-manifest.json", "absolute path")
        with tempfile.TemporaryDirectory() as base:
            bundle = make_bundle(base, extensions={"note": "for the secret-customer"})
            problems = CHECKER.check(bundle, private_strings=["Secret-Customer"])
            self.assertRefused(problems, "release-manifest.json")


class VendorDeliveriesAndReplacedModules(BundleTestCase):
    DELIVERY = {"project": "vendor-runtime", "delivery_tag": "delivery-2026-10", "hash_list_sha256": HASH_A}

    def test_optional_fields_are_accepted_when_well_formed(self):
        bundle = self.bundle(
            vendor_deliveries=[self.DELIVERY],
            replaced_modules=[{"original": "hal_nxp", "replacement": "hal_nxp_fork"}],
        )
        self.assertEqual([], CHECKER.check(bundle))

    def test_empty_lists_are_accepted(self):
        self.assertEqual([], CHECKER.check(self.bundle(vendor_deliveries=[], replaced_modules=[])))

    def test_a_delivery_needs_each_of_its_fields(self):
        for name in self.DELIVERY:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as base:
                delivery = {key: value for key, value in self.DELIVERY.items() if key != name}
                self.assertRefused(CHECKER.check(make_bundle(base, vendor_deliveries=[delivery])), name)

    def test_a_delivery_refuses_a_malformed_hash_list_hash_and_an_extra_field(self):
        for change in ({"hash_list_sha256": "xyz"}, {"licence": "x"}):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as base:
                delivery = {**self.DELIVERY, **change}
                problems = CHECKER.check(make_bundle(base, vendor_deliveries=[delivery]))
                self.assertRefused(problems, "vendor_deliveries[0]")

    def test_vendor_deliveries_must_be_a_list_of_objects(self):
        for value in ({}, "x", ["x"]):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, vendor_deliveries=value)), "vendor_deliveries")

    def test_a_replaced_module_needs_both_names(self):
        for entry in ({"original": "a"}, {"replacement": "b"}, {"original": "a", "replacement": "b", "x": 1}):
            with self.subTest(entry=entry), tempfile.TemporaryDirectory() as base:
                self.assertRefused(CHECKER.check(make_bundle(base, replaced_modules=[entry])), "replaced_modules[0]")


class HashesOnly(BundleTestCase):
    """The default mode refuses every form of the image, an ELF and a map."""

    def test_every_forbidden_suffix_is_refused_in_any_case(self):
        for suffix in sorted(CHECKER.FORBIDDEN_SUFFIXES):
            for name in (f"firmware{suffix}", f"sub/dir/FIRMWARE{suffix.upper()}"):
                with self.subTest(name=name), tempfile.TemporaryDirectory() as base:
                    bundle = make_bundle(base)
                    put_item(bundle, name, b"plain text\n")
                    self.assertRefused(CHECKER.check(bundle), name)

    def test_the_forbidden_suffixes_are_exactly_the_documented_ones(self):
        self.assertEqual(
            {".elf", ".map", ".bin", ".hex", ".srec", ".s19", ".uf2", ".img"}, set(CHECKER.FORBIDDEN_SUFFIXES)
        )

    def test_an_unlisted_forbidden_file_is_refused_too(self):
        bundle = self.bundle()
        (bundle / "zephyr.elf").write_bytes(b"\x7fELF")
        self.assertRefused(CHECKER.check(bundle), "zephyr.elf")

    def test_elf_magic_is_refused_under_a_text_name(self):
        bundle = self.bundle()
        put_item(bundle, "notes.txt", b"\x7fELF\x02\x01\x01 not text")
        self.assertRefused(CHECKER.check(bundle), "notes.txt")

    def test_the_bytes_of_the_image_are_refused_under_another_name(self):
        bundle = self.bundle()
        put_item(bundle, "firmware.dat", IMAGE)
        self.assertRefused(CHECKER.check(bundle), "firmware.dat", "image_sha256")

    def test_archives_are_refused(self):
        for name, data in (
            ("a.dat", b"PK\x03\x04" + b"A" * 20),
            ("b.dat", b"\x1f\x8b\x08" + b"A" * 20),
            ("c.dat", b"BZh9" + b"A" * 20),
            ("d.dat", b"\xfd7zXZ\x00" + b"A" * 20),
            ("e.dat", b"7z\xbc\xaf\x27\x1c" + b"A" * 20),
            ("f.dat", b"\x28\xb5\x2f\xfd" + b"A" * 20),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as base:
                bundle = make_bundle(base)
                put_item(bundle, name, data)
                self.assertRefused(CHECKER.check(bundle), name)

    def test_binary_content_under_a_text_name_is_refused(self):
        bundle = self.bundle()
        put_item(bundle, "payload", bytes(range(128, 256)) * 4)
        self.assertRefused(CHECKER.check(bundle), "payload", "UTF-8")

    def test_a_nul_byte_is_refused_although_it_is_valid_utf8(self):
        bundle = self.bundle()
        put_item(bundle, "plain.txt", b"looks like text\x00but has a NUL byte\n")
        self.assertRefused(CHECKER.check(bundle), "plain.txt", "NUL")

    def test_hex_and_srecord_text_are_refused(self):
        for name, text in (
            ("image.txt", ":10010000214601360121470136007EFE09D2190140\n:00000001FF\n"),
            ("image2.txt", "S1137AF00A0A0D0000000000000000000000000061\nS9030000FC\n"),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as base:
                bundle = make_bundle(base)
                put_item(bundle, name, text.encode())
                self.assertRefused(CHECKER.check(bundle), name)

    def test_a_symbolic_link_is_refused_even_to_text(self):
        bundle = self.bundle()
        target = self.base / "outside.txt"
        target.write_text("text\n")
        (bundle / "link.txt").symlink_to(target)
        self.assertRefused(CHECKER.check(bundle), "link.txt", "symbolic link")

    def test_there_is_no_mode_that_allows_images(self):
        result = run_cli(str(self.bundle()), "--mode", "included")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("unrecognized arguments", result.stderr)


class PathsAndPrivateStrings(BundleTestCase):
    def test_absolute_paths_are_refused(self):
        for text in (
            "path: /Users/someone/work/app\n",
            "path: /home/runner/work/product-x\n",
            "path: C:\\Users\\someone\\app\n",
            "path: C:/Users/someone/app\n",
            "path: \\\\server\\share\\app\n",
            "uri: file:///home/runner/work/app\n",
            "flags: -I/home/runner/work/app/include\n",
            "flags: -L/usr/lib\n",
            "env: PATH=/usr/bin:/home/runner/tools\n",
            "path: /Volumes/disk/app\n",
            "path: /github/workspace/app\n",
            "path: /srv/build/app\n",
            "path: /nix/store/abc\n",
            "path: /build/app\n",
            "flags: -O2 -I/home/runner/work/app/include\n",
            "-I/home/runner/work/app/include\n",
            "flags: -DX=1 -Wl,-L/usr/lib\n",
        ):
            with self.subTest(text=text), tempfile.TemporaryDirectory() as base:
                bundle = make_bundle(base)
                put_item(bundle, "pins-report.txt", (PINS + text).encode())
                self.assertRefused(CHECKER.check(bundle), "pins-report.txt")

    def test_urls_and_relative_paths_are_accepted(self):
        bundle = self.bundle()
        text = (
            PINS
            + "remote: https://github.com/zephyrproject-rtos/zephyr\n"
            + "remote: https://example.org/home/tmp/usr\n"
            + "remote: git://example.org/var/lib/x.git\n"
            + "remote: ssh://git@example.org/opt/x.git\n"
            + "path: modules/hal/nxp\n"
            + "path: product-x/build/app\n"
            + "path: a/home/b\n"
        )
        put_item(bundle, "pins-report.txt", text.encode())
        self.assertEqual([], CHECKER.check(bundle))

    def test_private_string_is_refused_without_regard_to_case(self):
        bundle = self.bundle()
        put_item(bundle, "pins-report.txt", (PINS + "note: the throwaway-private-name project\n").encode())
        self.assertEqual([], CHECKER.check(bundle))
        self.assertRefused(CHECKER.check(bundle, private_strings=["Throwaway-Private-Name"]), "pins-report.txt")

    def test_private_string_file_skips_comments_after_stripping(self):
        names = self.base / "private.txt"
        names.write_text("  # indented comment\n\nreal-name\n")
        self.assertEqual(["real-name"], CHECKER.read_private_strings(names))


class BundleContent(BundleTestCase):
    def test_item_changed_after_hashing_is_refused(self):
        bundle = self.bundle()
        (bundle / "west.lock.yml").write_text(LOCK + "# edited\n")
        self.assertRefused(CHECKER.check(bundle), "west.lock.yml", "hash differs")

    def test_item_missing_from_the_hash_list_is_refused(self):
        bundle = self.bundle()
        (bundle / "extra.txt").write_text("extra\n")
        self.assertRefused(CHECKER.check(bundle), "extra.txt", "not listed")

    def test_listed_item_missing_from_the_bundle_is_refused(self):
        bundle = self.bundle()
        (bundle / "west.lock.yml").unlink()
        self.assertRefused(CHECKER.check(bundle), "west.lock.yml", "not in the bundle")

    def test_items_in_subfolders_are_checked(self):
        bundle = self.bundle()
        put_item(bundle, "licences/zephyr/LICENSE.txt", b"Apache License\n")
        self.assertEqual([], CHECKER.check(bundle))
        (bundle / "licences/zephyr/LICENSE.txt").write_text("changed\n")
        self.assertRefused(CHECKER.check(bundle), "licences/zephyr/LICENSE.txt")

    def test_missing_manifest_is_refused(self):
        bundle = self.bundle()
        (bundle / "release-manifest.json").unlink()
        self.assertRefused(CHECKER.check(bundle), "release-manifest.json", "missing")

    def test_invalid_manifest_is_refused(self):
        for text in ("{", "[]", '"x"', "\xff\xfe"):
            with self.subTest(text=text), tempfile.TemporaryDirectory() as base:
                bundle = make_bundle(base)
                (bundle / "release-manifest.json").write_bytes(text.encode("latin-1"))
                self.assertRefused(CHECKER.check(bundle), "release-manifest.json")

    def test_a_bundle_folder_that_does_not_exist_is_refused(self):
        self.assertRefused(CHECKER.check(self.base / "absent"), "not a folder")


class SchemaOption(BundleTestCase):
    def schema_file(self, schema):
        path = self.base / "other.schema.json"
        path.write_text(json.dumps(schema))
        return path

    def test_another_schema_can_be_given(self):
        schema = {"type": "object", "required": ["colour"], "properties": {"colour": {"type": "string"}}}
        bundle = self.bundle()
        problems = CHECKER.check(bundle, schema_path=self.schema_file(schema))
        self.assertRefused(problems, "colour")
        result = run_cli(str(bundle), "--schema", str(self.schema_file(schema)))
        self.assertEqual(1, result.returncode)
        self.assertIn("colour", result.stderr)

    def test_a_schema_keyword_that_is_not_applied_is_refused(self):
        schema = {"type": "object", "oneOf": [{"required": ["x"]}]}
        problems = CHECKER.check(self.bundle(), schema_path=self.schema_file(schema))
        self.assertRefused(problems, "oneOf", "not supported")

    def test_an_unreadable_schema_is_refused(self):
        path = self.base / "bad.json"
        path.write_text("{")
        bundle = self.bundle()
        self.assertRefused(CHECKER.check(bundle, schema_path=path), "schema")
        self.assertRefused(CHECKER.check(bundle, schema_path=self.base / "none.json"), "schema")

    def test_the_default_schema_is_the_one_in_the_repository(self):
        self.assertEqual(SCHEMA.resolve(), Path(CHECKER.DEFAULT_SCHEMA).resolve())


class CommandLine(BundleTestCase):
    def test_a_good_bundle_passes_with_a_warning_about_the_private_string_list(self):
        result = run_cli(str(self.bundle()))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("no private-string list", result.stderr)
        self.assertIn("bundle check passed", result.stdout)

    def test_a_given_list_silences_the_warning_and_is_applied(self):
        bundle = self.bundle()
        put_item(bundle, "pins-report.txt", (PINS + "note: the throwaway-private-name project\n").encode())
        names = self.base / "private.txt"
        names.write_text("# a comment\n\nother-name\n")
        clean = run_cli(str(bundle), "--private-strings", str(names))
        self.assertEqual(0, clean.returncode, clean.stderr)
        self.assertNotIn("no private-string list", clean.stderr)
        names.write_text("# a comment\n\nthrowaway-private-name\n")
        found = run_cli(str(bundle), "--private-strings", str(names))
        self.assertEqual(1, found.returncode)
        self.assertIn("pins-report.txt", found.stderr)

    def test_a_bad_bundle_exits_with_one_and_names_the_item(self):
        bundle = self.bundle()
        (bundle / "zephyr.elf").write_bytes(b"\x7fELF")
        result = run_cli(str(bundle))
        self.assertEqual(1, result.returncode)
        self.assertIn("zephyr.elf", result.stderr)
        self.assertNotIn("bundle check passed", result.stdout)

    def test_an_unreadable_private_string_list_is_an_error_not_a_pass(self):
        result = run_cli(str(self.bundle()), "--private-strings", str(self.base / "absent.txt"))
        self.assertEqual(2, result.returncode)

    def test_the_check_leaves_the_bundle_unchanged(self):
        bundle = self.bundle()
        before = {p.relative_to(bundle).as_posix(): p.read_bytes() for p in bundle.rglob("*") if p.is_file()}
        run_cli(str(bundle))
        after = {p.relative_to(bundle).as_posix(): p.read_bytes() for p in bundle.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_the_script_names_its_source_and_licence(self):
        head = SCRIPT.read_text()[:1500]
        self.assertIn("SPDX-License-Identifier: Apache-2.0", head)
        self.assertIn("wonny-labs/crux-controller", head)
        self.assertIn("f03dc79", head)
        self.assertIn("release/check_bundle.py", head)
        self.assertTrue(re.search(r"\bAdapted\b|\bPorted\b", head))


if __name__ == "__main__":
    unittest.main()
