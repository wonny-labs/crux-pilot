# Static frame size of each function: pushed registers plus the first "sub sp" of its prologue. A lower bound
# on a call chain, which adds frames, and it ignores alloca and variable-length arrays.
import re, subprocess, sys
elf, out = sys.argv[1], sys.argv[2]
SDK = '/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump'
text = subprocess.run([SDK, '-d', '--no-show-raw-insn', elf], capture_output=True, text=True).stdout
frames = {}
name = None; pushed = 0; sub = 0; seen = 0
def flush():
    if name and (pushed or sub): frames[name] = pushed * 4 + sub
for line in text.split('\n'):
    m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
    if m:
        flush(); name = m.group(1); pushed = 0; sub = 0; seen = 0; continue
    if name is None or seen > 12: continue
    m = re.search(r'\t(push|push\.w|stmdb)\s+(?:sp!, )?\{([^}]*)\}', line)
    if m and not pushed:
        pushed = len([r for r in m.group(2).split(',') if r.strip()])
    m = re.search(r'\tsub(?:w|\.w)?\s+sp, (?:sp, )?#(\d+)', line)
    if m and not sub:
        sub = int(m.group(1))
    seen += 1
flush()
with open(out, 'w') as f:
    for n, s in sorted(frames.items(), key=lambda kv: -kv[1])[:200]:
        f.write(f'{s:6d} {n}\n')
print(len(frames), 'functions')
