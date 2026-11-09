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


def build_object(directory, defined, undefined=(), data=()):
    """An object file that defines functions and data and references others, built by the host compiler."""
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
    return obj


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
        for name in RSA_NAMES:
            as_function = build_object(scratch, KEPT + [name])
            result = run(script, as_function)
            if result.returncode != 1 or name not in result.stderr + result.stdout:
                wrong.append(f"{name} as a function was not refused and named: rc={result.returncode}")
            as_data = build_object(scratch, KEPT, data=[name])
            result = run(script, as_data)
            if result.returncode != 1 or name not in result.stderr + result.stdout:
                wrong.append(f"{name} as data was not refused and named: rc={result.returncode}")
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
    for old, new, why in (
            ('if kind in ("U", "u")', 'if False', "undefined symbols are counted"),
            ("sys.exit(2)", "sys.exit(0)", "an unreadable image passes"),
            ("if not names:", "if False:", "an image with no symbols passes"),
            ("return 1", "return 0", "an offender does not fail the check"),
    ):
        if old in text:
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
                copy = Path(scratch) / f"mutant{count}.py"
                copy.write_text(text)
                self.assertNotEqual(checks(copy), [], f"mutant survived: {description}")
        self.assertGreaterEqual(count, 8, "the mutants of the script were not found")


if __name__ == "__main__":
    unittest.main()
