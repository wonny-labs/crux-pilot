#!/usr/bin/env python3
"""Check a built Zephyr image against the shell rule of the command line contract (rule L5).

The Zephyr shell stays a development facility and never reaches the network.

| Image                                     | CONFIG_SHELL | Backends                          |
| ----------------------------------------- | ------------ | --------------------------------- |
| Has management, and is not marked         | must be n    | none                              |
| Has management, and is marked development | may be y     | SHELL_BACKEND_SERIAL only         |
| Has no management                         | may be y     | any except the network backends   |

No image of any kind has a network backend or MCUMGR_GRP_SHELL.

With --elf or --nm-output, it also inspects a production image: no shell symbol, no shell backend
instance and no shell command section is linked. The Zephyr linker script always places the shell's
iterable sections, so their start and end markers exist in every image and must enclose nothing. A
missing marker is a failure, because a stripped image or an empty listing would otherwise pass having
shown nothing. Pass --elf only for an image that the first row applies to.

cmake/CruxManagementShell.cmake applies the same table at configuration, and
components/cli/ports/zephyr/cli_shell_rule.c at compilation. A project's own SHELL_DEFINE backend
has no Kconfig symbol, so this script sees one only through the image inspection.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

MANAGEMENT = ("CRUX_CLI", "CRUX_WEB", "CRUX_SSH")
DEVELOPMENT = "CRUX_DEVELOPMENT_IMAGE"
FORBIDDEN_EVERYWHERE = ("SHELL_BACKEND_TELNET", "SHELL_BACKEND_WEBSOCKET", "SHELL_BACKEND_MQTT",
                        "MCUMGR_GRP_SHELL")
BACKEND = re.compile(r"^SHELL_BACKEND_")
ALLOWED_BACKEND = re.compile(r"^SHELL_BACKEND_SERIAL(_|$)")
SETTING = re.compile(r"^CONFIG_([A-Za-z0-9_]+)=(.*)$")
SHELL_SECTIONS = ("shell", "shell_root_cmds", "shell_subcmds", "shell_dynamic_subcmds")
SHELL_SYMBOL = re.compile(r"^(_{0,2}|z_)shell(_|$)")


def read_config(text):
    """The symbols a .config sets. A symbol that is not set has no CONFIG_X= line, or says =n."""
    settings = {}
    for line in text.splitlines():
        match = SETTING.match(line.strip())
        if match and match.group(2) != "n":
            settings[match.group(1)] = match.group(2)
    return settings


def config_errors(settings):
    # Zephyr always sets something, so this is the wrong file, and it would otherwise pass.
    if not settings:
        return ["the .config holds no settings"]
    errors = [f"CONFIG_{symbol} is set: no image may have it"
              for symbol in FORBIDDEN_EVERYWHERE if symbol in settings]
    if not any(symbol in settings for symbol in MANAGEMENT):
        return errors
    development = DEVELOPMENT in settings
    if not development and "SHELL" in settings:
        errors.append("CONFIG_SHELL is set in a management image without "
                      f"CONFIG_{DEVELOPMENT}")
    for symbol in sorted(settings):
        if not BACKEND.match(symbol) or symbol in FORBIDDEN_EVERYWHERE:
            continue
        if not development:
            errors.append(f"CONFIG_{symbol} is set in a management image without "
                          f"CONFIG_{DEVELOPMENT}")
        elif not ALLOWED_BACKEND.match(symbol):
            errors.append(f"CONFIG_{symbol} is set: a development image with management may "
                          "use the serial backend only")
    return errors


def read_symbols(text):
    """The defined symbols in nm output, as a name to address map. nm prints an undefined symbol with
    no address, so it is not here: a reference is not a linked symbol, and is not a marker either."""
    symbols = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = fields[0]
    return symbols


def image_errors(symbols):
    errors = []
    markers = set()
    for section in SHELL_SECTIONS:
        start, end = f"_{section}_list_start", f"_{section}_list_end"
        markers.update((start, end))
        missing = [marker for marker in (start, end) if marker not in symbols]
        errors += [f"the marker {marker} is missing, so the image shows nothing" for marker in missing]
        if not missing and int(symbols[start], 16) != int(symbols[end], 16):
            errors.append(f"the {section} section holds entries")
    for name in sorted(symbols):
        if name not in markers and SHELL_SYMBOL.match(name):
            errors.append(f"shell symbol linked: {name}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", type=Path, required=True, help="The image's .config")
    parser.add_argument("--elf", type=Path, help="The linked image, read with nm")
    parser.add_argument("--nm", default="nm", help="nm for --elf")
    parser.add_argument("--nm-output", type=Path, help="Saved nm output, in place of --elf")
    args = parser.parse_args()

    try:
        errors = config_errors(read_config(args.config.read_text(encoding="utf-8")))
    except OSError as error:
        errors = [f"could not read {args.config}: {error}"]
    if args.elf is not None:
        try:
            result = subprocess.run([args.nm, str(args.elf)], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True, check=False)
        except OSError as error:
            errors.append(f"nm could not read {args.elf}: {error}")
        else:
            if result.returncode != 0:
                errors.append(f"nm could not read {args.elf}: {result.stderr.strip()}")
            else:
                errors += image_errors(read_symbols(result.stdout))
    if args.nm_output is not None:
        errors += image_errors(read_symbols(args.nm_output.read_text(encoding="utf-8")))
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f"Management shell rule failed: {len(errors)} problem(s).", file=sys.stderr)
        return 1
    print("Management shell rule passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
