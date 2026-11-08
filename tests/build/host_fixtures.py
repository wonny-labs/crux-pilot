"""Real output of the read-only host commands that the stage 1 code parses, captured on 2026-11-08.

``fixtures/host-2026-11-08/commands.json`` holds command, exit status, stdout and stderr as the Mac
produced them (only the process id of lsof is a ``%PID%`` placeholder, user names, hex digits of the
boot session UUID and process lists are trimmed, and the two entries marked PROXY are real output of
the active DHCP interface with the bench identity substituted). The fakes of the tests are built from
these, so a parser that disagrees with the real tool fails here and not on the first real run.
"""
import json
import os
import plistlib
import subprocess
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parent / 'fixtures' / 'host-2026-11-08'
_COMMANDS = json.loads((DIRECTORY / 'commands.json').read_text())


def text(name):
    return _COMMANDS[name]['stdout']


def completed(name, pid=None):
    entry = _COMMANDS[name]
    return subprocess.CompletedProcess(entry['command'], entry['returncode'],
                                       entry['stdout'].replace('%PID%', str(os.getpid() if pid is None else pid)), entry['stderr'])


def daplink_tree():
    return plistlib.loads((DIRECTORY / 'ioreg-daplink-subtree.plist.xml').read_bytes())


def comports():
    return json.loads((DIRECTORY / 'comports-daplink.json').read_text())
