"""The symbol check of the RT1060-EVKB bench image refuses an image that links RSA code (row C9, D11).

tests/zephyr/management_rt1060_compile/check_bench_rsa_symbols.py reads the symbol table of the linked image. This
test builds small objects with the host compiler, with and without RSA names, and runs the script on them. It then
weakens the script in a copy, one rule at a time, and the same checks must fail: a rule that no check exercises
would survive. Nothing here touches a board.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tests/zephyr/management_rt1060_compile/check_bench_rsa_symbols.py"

# What the profile still links, with names that hold the letters "rsa" inside a longer word, which must pass.
KEPT = ["wc_ecc_make_key_ex", "sp_ecc_sign_256", "sp_256_mont_mul_9", "wc_ed25519_sign_msg_ex", "psa_generate_key",
        "psa_import_key", "wolfpsa_infer_key_bits", "wc_PBKDF2", "wolfSSL_accept", "SendTls13CertificateVerify",
        "tls_x509_universal_string", "list_traversal", "reversal_count", "parsable_value", "DecodeUniversalString",
        "TAG_UNIVERSAL", "versatile_helper", "Parsable", "sp_ecc_verify_256"]
# What the image linked before row C9 (names read from the link map of the image of the C5 tree).
RSA_NAMES = ["wc_MakeRsaKey", "wc_RsaPrivateKeyDecode", "_RsaPrivateKeyDecode", "wc_RsaPSS_Sign", "wc_RsaFunction_ex",
             "RsaPublicEncryptEx", "RsaSign", "DecodeRsaPssParams", "psa_asymmetric_generate_key_rsa",
             "sp_RsaPrivate_3072", "sp_RsaPublic_2048", "sp_ModExp_3072", "sp_ModExp_2048", "sp_3072_mod_exp_53",
             "sp_2048_div_word_36", "sp_4096_mul_128", "wolfpsa_rsa_import", "rsa_key_table", "wc_InitRsaKey",
             "WC_RSA_PSS_TABLE", "wolfSSL_RSA_new", "wolfpsa_pk_rsa_verify"]


_OBJECTS = {}
_CACHE = []


def tearDownModule():
    for directory in _CACHE:
        shutil.rmtree(directory, ignore_errors=True)


def build_object(directory, defined, undefined=(), data=()):
    """An object file that defines functions and data and references others, built by the host compiler. The
    same object is built once, because every mutant of the script is run against the same objects."""
    key = (tuple(defined), tuple(undefined), tuple(data))
    if key in _OBJECTS:
        return _OBJECTS[key]
    if not _CACHE:
        _CACHE.append(tempfile.mkdtemp())
    directory = Path(_CACHE[0]) / f"object{len(_OBJECTS)}"
    directory.mkdir()
    source = Path(directory) / "probe.c"
    lines = []
    for name in undefined:
        lines.append(f"extern int {name}(void);")
    for name in defined:
        lines.append(f"int {name}(void) {{ return 0; }}")
    for name in data:
        lines.append(f"const int {name} = 7;")
    if undefined:
        lines.append("int uses_undefined(void) { return " + " + ".join(f"{n}()" for n in undefined) + "; }")
    source.write_text("\n".join(lines) + "\n")
    obj = Path(directory) / "probe.o"
    subprocess.run(["cc", "-c", "-O0", "-o", str(obj), str(source)], check=True, capture_output=True)
    _OBJECTS[key] = obj
    return obj


def fake_nm(directory, name, lines):
    """A stand-in nm that prints `nm -P -S` lines (name, type, value, size), for what a host object cannot hold."""
    path = Path(directory) / name
    path.write_text("#!/bin/sh\n" + "".join(f"echo '{line}'\n" for line in lines))
    path.chmod(0o755)
    return path


# What the trimmed bench image holds that still has RSA in its name (the real image's nm, sizes in hex).
LEFTOVERS = ["psa_asymmetric_sign_rsa T 60073568 0000000e", "BEGIN_RSA_PRIV r 6008768f 00000020",
             "BEGIN_RSA_PUB r 60087652 0000001f", "END_RSA_PRIV r 60087671 0000001e",
             "END_RSA_PUB r 60087635 0000001d"]
BASE = ["wc_ecc_make_key_ex T 60010000 00000100", "sp_ecc_sign_256 T 60011000 00000200"]


def run(script, obj, *extra):
    return subprocess.run([sys.executable, str(script), *extra, str(obj)], capture_output=True, text=True, check=False)


def checks(script):
    """Every behaviour of the script. Returns the list of what is wrong, so a mutant must make it non-empty."""
    wrong = []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        clean = build_object(scratch, KEPT)
        result = run(script, clean)
        if result.returncode != 0:
            wrong.append(f"a clean image was refused: {result.stdout}{result.stderr}")
        # Every RSA name, once as functions and once as data, in one image: the report must name each of them.
        for kind, image in (("functions", build_object(scratch, KEPT + RSA_NAMES)),
                            ("data", build_object(scratch, KEPT, data=RSA_NAMES))):
            result = run(script, image)
            report = result.stderr + result.stdout
            missed = [name for name in RSA_NAMES if name not in report]
            if result.returncode != 1 or missed:
                wrong.append(f"RSA names as {kind}: rc={result.returncode}, not named: {missed}")
        # A reference alone cannot link without a definition, so it is not what the check looks for.
        referenced = build_object(scratch, KEPT, undefined=["wc_MakeRsaKey"])
        result = run(script, referenced)
        if result.returncode != 0:
            wrong.append(f"an undefined reference was counted as a symbol of the image: {result.stdout}{result.stderr}")
        # Fail closed: nothing to read is an error and never a pass.
        silent = scratch / "silent-nm"
        silent.write_text("#!/bin/sh\nexit 0\n")
        silent.chmod(0o755)
        result = run(script, clean, "--nm", str(silent))
        if result.returncode != 2:
            wrong.append(f"an image with no symbols passed: rc={result.returncode}")
        only_references = scratch / "references-nm"
        only_references.write_text("#!/bin/sh\necho 'wc_MakeRsaKey U'\necho 'other w'\n")
        only_references.chmod(0o755)
        result = run(script, clean, "--nm", str(only_references))
        if result.returncode != 2:
            wrong.append(f"an image with references only passed: rc={result.returncode}")
        result = run(script, scratch / "missing.elf")
        if result.returncode != 2:
            wrong.append(f"a missing file passed: rc={result.returncode}")
        result = run(script, clean, "--nm", str(scratch / "no-such-nm"))
        if result.returncode != 2:
            wrong.append(f"a missing nm passed: rc={result.returncode}")
        failing = scratch / "failing-nm"
        failing.write_text("#!/bin/sh\nexit 3\n")
        failing.chmod(0o755)
        result = run(script, clean, "--nm", str(failing))
        if result.returncode != 2:
            wrong.append(f"an nm that fails passed: rc={result.returncode}")
        # The five names that the trimmed image keeps pass as what they are, and as nothing else.
        small = ["psa_asymmetric_verify_rsa T 60073600 00000010", "psa_asymmetric_encrypt_rsa t 60073620 00000040",
                 "psa_asymmetric_decrypt_rsa T 60073680 00000012"]
        good = fake_nm(scratch, "leftover-nm", BASE + LEFTOVERS + small)
        result = run(script, clean, "--nm", str(good))
        if result.returncode != 0:
            wrong.append(f"the stubs and PEM labels of the trimmed image were refused: {result.stdout}{result.stderr}")
        for why, line in (
                ("a stub as large as the real function", "psa_asymmetric_sign_rsa T 60073568 0000028c"),
                ("a stub with no size", "psa_asymmetric_sign_rsa T 60073568"),
                ("a stub of size zero", "psa_asymmetric_sign_rsa T 60073568 00000000"),
                ("a stub that is data", "psa_asymmetric_sign_rsa r 60073568 0000000e"),
                ("a second stub name as the real function", "psa_asymmetric_verify_rsa T 60073568 000000c8"),
                ("an encrypt stub as the real function", "psa_asymmetric_encrypt_rsa T 60073568 00000200"),
                ("a decrypt stub as the real function", "psa_asymmetric_decrypt_rsa T 60073568 00000200"),
                ("a PEM label as code", "BEGIN_RSA_PRIV T 6008768f 00000020"),
                ("a PEM label as a large table", "BEGIN_RSA_PRIV r 6008768f 00001000"),
                ("a PEM label with no size", "END_RSA_PUB r 60087635"),
                ("another RSA name that only looks like a label", "BEGIN_RSA_KEY r 6008768f 00000020"),
                ("a stub with another name", "psa_asymmetric_wrap_rsa T 60073568 0000000e"),
                ("a Mach-O name of the single-precision RSA math", "_sp_ModExp_3072 T 60073568 00000220"),
                ("a Mach-O name of an RSA function", "_wc_MakeRsaKey T 60073568 000003c4")):
            bad = fake_nm(scratch, "bad-nm", BASE + [line])
            result = run(script, clean, "--nm", str(bad))
            if result.returncode != 1:
                wrong.append(f"{why} was not refused: rc={result.returncode}")
        # The report names every offender, not only the first.
        two = build_object(scratch, KEPT + ["wc_MakeRsaKey", "sp_ModExp_3072"])
        result = run(script, two)
        if not ("wc_MakeRsaKey" in result.stderr + result.stdout and "sp_ModExp_3072" in result.stderr + result.stdout):
            wrong.append("the report does not name every offender")
    return wrong


def mutants(text):
    """(description, mutated text): each weakens one rule of the script."""
    lines = text.splitlines(keepends=True)
    start = next(i for i, line in enumerate(lines) if line.startswith("DENY = ("))
    end = next(i for i in range(start, len(lines)) if lines[i].startswith(")"))
    for number in range(start + 1, end):
        rest = lines[:number] + lines[number + 1:]
        yield f"pattern line {number + 1} is removed: {lines[number].strip()[:60]}", "".join(rest)
    allowed = next(i for i, line in enumerate(lines) if line.startswith("ALLOWED = {"))
    allowed_end = next(i for i in range(allowed, len(lines)) if lines[i].startswith("}"))
    for number in range(allowed + 1, allowed_end):
        rest = lines[:number] + lines[number + 1:]
        yield f"allowance line {number + 1} is removed: {lines[number].strip()[:60]}", "".join(rest)
    for old, new, why in (
            ('if kind in ("U", "u", "w", "v")', 'if False', "undefined and weak symbols are counted"),
            ('if kind in ("U", "u", "w", "v")', 'if kind in ("U", "u")', "weak symbols are counted"),
            ("sys.exit(2)", "sys.exit(0)", "an unreadable image passes"),
            ("if not symbols:", "if False:", "an image with no symbols passes"),
            ("return 1\n", "return 0\n", "an offender does not fail the check"),
            ("if kind in kinds and 0 < size <= largest:", "if 0 < size <= largest:", "an allowed name passes as any kind"),
            ("if kind in kinds and 0 < size <= largest:", "if kind in kinds and size <= largest:",
             "an allowed name passes with no size"),
            ("if kind in kinds and 0 < size <= largest:", "if kind in kinds and 0 < size:",
             "an allowed name passes at any size"),
            ("if kind in kinds and 0 < size <= largest:", "if kind in kinds and 0 < size < largest:",
             "an allowed name at its largest size is refused"),
            ("for form in forms:\n        if form in ALLOWED:", "for form in forms[:0]:\n        if form in ALLOWED:",
             "no allowance applies"),
            ("forms = [name, name[1:]] if name.startswith(\"_\") else [name]", "forms = [name]",
             "the Mach-O underscore is not stripped"),
    ):
        if old not in text:
            yield f"NOT APPLICABLE: {why}", None
            continue
        yield why, text.replace(old, new)


class RsaSymbolCheck(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc") and shutil.which("nm"), "needs a C compiler and nm")
    def test_the_check_passes_a_clean_image_and_refuses_each_rsa_name(self):
        self.assertEqual(checks(SCRIPT), [])

    @unittest.skipUnless(shutil.which("cc") and shutil.which("nm"), "needs a C compiler and nm")
    def test_every_weakened_rule_is_caught(self):
        original = SCRIPT.read_text()
        count = 0
        with tempfile.TemporaryDirectory() as scratch:
            for description, text in mutants(original):
                count += 1
                self.assertIsNotNone(text, description)
                copy = Path(scratch) / f"mutant{count}.py"
                copy.write_text(text)
                self.assertNotEqual(checks(copy), [], f"mutant survived: {description}")
        self.assertGreaterEqual(count, 8, "the mutants of the script were not found")


if __name__ == "__main__":
    unittest.main()
