"""Weakens bench_first_account_file.py in a MIRROR of the two files (not in the work tree), one change at a time, and runs its unit tests."""
import subprocess, sys, os
mirror = sys.argv[1]
os.chdir(mirror)
p = 'tests/zephyr/management_rt1060_compile/bench_first_account_file.py'
orig = open(p).read()
muts = {
 "drop the hex needles": ('("hex", password.hex().encode()),\n             ("hex", password.hex().upper().encode()),\n', ''),
 "drop the reversed needle": ('("reversed", password[::-1]),\n', ''),
 "drop two of three base64 alignments": ('for lead in range(3):', 'for lead in range(1):'),
 "an image file may hold anything": ('if kind == "raw" and image and not hex_text:', 'if image:'),
 "the image count is not checked": ('if count != 1:', 'if count < 0:'),
 "the mode is not checked": ('if info.st_mode & 0o077:', 'if False:'),
 "the work tree is not checked": ('if _inside_work_tree(resolved) is not None:', 'if False:'),
 "the public test password is accepted": ('if hashlib.sha256(body).hexdigest() == PUBLIC_TEST_PASSWORD_SHA256:', 'if False:'),
 "no minimum length": ('if len(body) < LENGTH_MIN:', 'if len(body) < 1:'),
 "links are followed": ('if not path.is_symlink() and path.is_file():', 'if path.is_file():'),
 "the path of the file is not looked for": ('if include_path is not None and include_path in data:', 'if False:'),
 "the manifest hash is not compared": ('if recorded != actual:', 'if False:'),
 "no UTF-16 needle": ('("utf-16", text.encode("utf-16-le")),\n', ''),
}
for name, (old, new) in muts.items():
    assert old in orig, name
    open(p, 'w').write(orig.replace(old, new, 1))
    r = subprocess.run([sys.executable, '-m', 'unittest', 'tests/build/test_bench_first_account_file.py'], capture_output=True, text=True)
    print(("killed   " if r.returncode else "SURVIVED ") + name)
open(p, 'w').write(orig)
r = subprocess.run([sys.executable, '-m', 'unittest', 'tests/build/test_bench_first_account_file.py'], capture_output=True, text=True)
print("restored copy passes" if r.returncode == 0 else "RESTORED COPY FAILS")
