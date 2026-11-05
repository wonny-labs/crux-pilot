#!/usr/bin/env python3
"""The entry points reach the command line through one door, and hold no route to a Zephyr shell.

Reads the undefined symbols of the real entry points' object files, as built, and refuses anything but the
session functions of the CLI's adapter and the attachment. A source check could be fooled by a macro, and
this one cannot, but it says nothing about what those functions call. Tests of the shell's built-in commands
cover that. A compiler helper, a sanitizer or a C library copy is allowed, because none of them leads anywhere.
"""
import argparse
import re
import subprocess
import sys

ALLOWED = {
    "crux_cli_run",
    "crux_cli_exec",
    "crux_cli_write_terminal",
    "crux_ssh_attachment_session",
}
COMPILER = re.compile(r"^(?:__asan|__ubsan|__sanitizer|__tsan|__stack_chk|_GLOBAL_OFFSET_TABLE_$|memset$|memcpy$)")


def undefined(nm, path):
    output = subprocess.run([nm, "--undefined-only", path], check=True, capture_output=True, text=True).stdout
    return {line.split()[-1] for line in output.splitlines() if line.strip() and not line.endswith(":")}


def unexpected(symbols):
    return sorted(name for name in symbols if name not in ALLOWED and not COMPILER.match(name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True)
    parser.add_argument("objects", nargs="+")
    arguments = parser.parse_args()
    symbols = set()
    for path in arguments.objects:
        symbols |= undefined(arguments.nm, path)
    errors = unexpected(symbols)
    for name in errors:
        print(f"{name}: the CLI's SSH entry points must reach only the session functions", file=sys.stderr)
    for name in sorted(ALLOWED - symbols):
        print(f"{name}: expected and not used, so this check no longer sees the real entry points", file=sys.stderr)
        errors.append(name)
    # The check must be able to fail.
    if not unexpected({"shell_execute_cmd"}) or not unexpected({"z_shell_print"}):
        errors.append("the check accepts a shell symbol")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
