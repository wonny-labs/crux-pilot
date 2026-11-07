#!/usr/bin/env python3
"""The provider type check must catch an SSH library name, on a copy of the files it reads."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

CHECK = Path(__file__).with_name("check_provider_types.py")


def run(root):
    return subprocess.run([sys.executable, str(CHECK), "--root", str(root)], capture_output=True,
                          text=True, check=False)


def expect(condition, message):
    if not condition:
        print(message, file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    source = parser.parse_args().root.resolve() / "components/ssh"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        shutil.copytree(source, root / "components/ssh")
        expect(run(root).returncode == 0, "the repository's own files did not pass")

        for name, text in (("include.c", "#include <wolfssh/ssh.h>\n"),
                           ("type.h", "WOLFSSH *session;\n"),
                           ("call.c", "void f(void) { wolfSSH_free(0); }\n"),
                           ("status.c", "int x = WS_SUCCESS;\n")):
            leak = root / "components/ssh/cli" / name
            leak.write_text(text)
            result = run(root)
            expect(result.returncode == 1 and name in result.stderr, f"{name} was not caught")
            leak.unlink()

        comment = root / "components/ssh/cli/comment.c"
        comment.write_text("/* wolfSSH may be named in a comment. */\n")
        expect(run(root).returncode == 0, "a comment was refused")
        comment.unlink()

        allowed = root / "components/ssh/sftp/port/myFilesystem.h"
        allowed.unlink()
        expect(run(root).returncode == 1, "a missing allowed file was not reported")
    print("The provider type check catches each kind of leak.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
