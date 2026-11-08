"""Real output of the read-only host commands that the stage 1 code parses, captured on 2026-11-08.

``fixtures/host-2026-11-08/commands.json`` holds command, exit status, stdout and stderr as the Mac
produced them. Edits: the process id of lsof is a ``%PID%`` placeholder, user names, hex digits of the
boot session UUID and process lists are trimmed. Entries with a ``note`` starting with PROXY are not
captured from the bench itself: the ``lsof_cu_held_*`` entries hold a scratch file open in the capturing
Python process in place of the cu node (the UART was never opened for this), and three entries are real
output of the active DHCP interface en0 with the bench identity substituted (ifconfig, connected network
route, cloned host route). The fakes of the tests are built from these, so a parser that disagrees with
the real tool fails here and not on the first real run.

``jlink-loadfile-verifybin-restart.log`` is a real J-Link Commander V9.86a transcript of this probe and
target (``usb 602003106``, ``loadfile``, ``verifybin``, ``r``, ``g``, ``q``), copied from the owner's local
backup folder (``crux-backup/rt1060/codesys-night-20261104/n50-restore.log``, file dated 2026-11-05) with the
image path replaced by ``<image>.bin``; it is another firmware and another day than this run.
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
