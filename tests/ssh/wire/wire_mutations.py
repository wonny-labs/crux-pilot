#!/usr/bin/env python3
"""Issue #192: each mutant of the SSH wire checks must fail their suite.

Every mutant names one text that must occur exactly once in crux_ssh_wire.c, and its replacement. The
runner first builds the source as it is and expects the suite to pass, then builds each mutant apart and
expects the suite to fail. A mutant whose text no longer occurs is an error, so that an edit of the
source cannot quietly retire a mutant. The suite runs with fewer random rounds here, and under
AddressSanitizer and UndefinedBehaviorSanitizer, so that a mutant that only reads past a buffer fails.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROUNDS = 20000
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]

# (what the mutant breaks, text in the source, replacement)
MUTANTS = (
    # Key types: the owner's decision is Ed25519 and ECDSA P-256 only, and no RSA.
    ("RSA accepted as an Ed25519 name",
     'static const char name_ed25519[] = "ssh-ed25519";',
     'static const char name_ed25519[] = "ssh-rsa";'),
    ("P-384 accepted in place of P-256",
     'static const char curve_p256[] = "nistp256";',
     'static const char curve_p256[] = "nistp384";'),
    ("the algorithm name not compared with the blob",
     "if (!string_is(algorithm, algorithm_length, form->name)) {",
     "if (false) {"),
    ("the blob's type name not checked",
     "!string_is(text, text_length, form->name)",
     "false"),
    ("the curve name not checked",
     "!string_is(text, text_length, form->curve)",
     "false"),
    ("any point length accepted",
     "text_length != form->point_length",
     "false"),
    ("a compressed point accepted",
     "text[0] != POINT_UNCOMPRESSED",
     "false"),
    ("bytes after the key accepted",
     "return reader.position == reader.length;",
     "return true;"),
    ("a length past the end accepted",
     "if (claimed > reader->length - reader->position) {",
     "if (false) {"),
    # User names.
    ("an empty name accepted",
     "if (name == NULL || user == NULL || length == 0u || length > CRUX_SSH_USER_NAME_MAX ||",
     "if (name == NULL || user == NULL || length > CRUX_SSH_USER_NAME_MAX ||"),
    ("a name one byte too long accepted",
     "length > CRUX_SSH_USER_NAME_MAX ||",
     "length > CRUX_SSH_USER_NAME_MAX + 1u ||"),
    ("an embedded NUL in a name accepted",
     "memchr(name, '\\0', length) != NULL) {",
     "false) {"),
    # Requests.
    ("a NUL in a command accepted",
     "memchr(*command, '\\0', *command_length) != NULL",
     "false"),
    ("a command over the limit accepted",
     "*command_length > command_max",
     "false"),
    ("a string that does not fill the request accepted",
     "return reader.position == reader.length ? text : NULL;",
     "return text;"),
    ("another subsystem accepted",
     'return text != NULL && string_is(text, text_length, "sftp");',
     "return text != NULL;"),
    # Host keys.
    ("a host key of the wrong length accepted",
     "host_key_length == CRUX_SSH_WIRE_HOST_KEY_P256_BYTES &&",
     "host_key_length >= CRUX_SSH_WIRE_HOST_KEY_P256_BYTES &&"),
    ("a P-256 host key without an uncompressed point accepted",
     "host_key[CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES] == POINT_UNCOMPRESSED",
     "true"),
    ("a short blob buffer accepted",
     "if (blob == NULL || blob_length == NULL || capacity < needed) {",
     "if (blob == NULL || blob_length == NULL) {"),
    ("the private part put into the blob",
     "const uint8_t *public_part = host_key + CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES;",
     "const uint8_t *public_part = host_key;"),
    # Fingerprints.
    ("the last Base64 character dropped",
     "if (bit_count > 0u) {",
     "if (false) {"),
    ("a short fingerprint buffer accepted",
     "size < CRUX_SSH_WIRE_FINGERPRINT_SIZE",
     "size < 8u"),
)


def build_and_run(compiler, source, test, includes, directory, name):
    binary = Path(directory) / name
    # No -Werror: a mutant may leave a variable unused. The real build keeps it.
    command = [compiler, "-std=c17", "-O1", "-g", "-UNDEBUG", f"-DWIRE_TEST_ROUNDS={ROUNDS}u"]
    command += SANITIZERS
    command += [f"-I{include}" for include in includes]
    command += [str(source), str(test), "-o", str(binary)]
    built = subprocess.run(command, capture_output=True, text=True, check=False)
    if built.returncode != 0:
        return "build", built.stderr
    ran = subprocess.run([str(binary)], capture_output=True, text=True, check=False, timeout=120)
    return ("pass" if ran.returncode == 0 else "fail"), ran.stdout + ran.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--test", type=Path, required=True)
    parser.add_argument("--include", action="append", default=[])
    arguments = parser.parse_args()
    original = arguments.source.read_text(encoding="utf-8")
    errors = []
    with tempfile.TemporaryDirectory() as directory:
        # The build's compiler first. Some installations of Clang lack the sanitizer runtimes, and
        # GCC then stands in. With neither, the runner fails: a mutant must not pass for want of one.
        compiler = None
        for candidate in dict.fromkeys((arguments.compiler, "gcc")):
            result, output = build_and_run(candidate, arguments.source, arguments.test,
                                           arguments.include, directory, "original")
            if result == "pass":
                compiler = candidate
                break
            print(f"{candidate}: the source as it is did not pass ({result}):\n{output}",
                  file=sys.stderr)
        if compiler is None:
            return 1
        print(f"Compiler: {compiler}, with {' '.join(SANITIZERS)}")
        for index, (what, text, replacement) in enumerate(MUTANTS):
            count = original.count(text)
            if count != 1:
                errors.append(f"mutant {index} ({what}): its text occurs {count} times")
                continue
            mutant = Path(directory) / f"mutant_{index}.c"
            mutant.write_text(original.replace(text, replacement), encoding="utf-8")
            result, output = build_and_run(compiler, mutant, arguments.test,
                                           arguments.include, directory, f"mutant_{index}")
            if result == "pass":
                errors.append(f"mutant {index} ({what}): survived")
            elif result == "build":
                errors.append(f"mutant {index} ({what}): did not build\n{output}")
            else:
                print(f"killed: {what}")
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"All {len(MUTANTS)} mutants of the wire checks fail the suite.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
