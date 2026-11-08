# Static worst-case stack along direct calls, from the disassembly. Indirect calls (function pointers) are NOT
# followed, so a chain through a callback is cut short and the figure is a lower bound for that root. The frame of
# a function is its pushed registers plus its first "sub sp". Recursion is cut at the first repeat.
import re, subprocess, sys, json
elf, roots_file, out = sys.argv[1], sys.argv[2], sys.argv[3]
OBJ = '/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump'
text = subprocess.run([OBJ, '-d', '--no-show-raw-insn', elf], capture_output=True, text=True).stdout
frame = {}; calls = {}
name = None; pushed = 0; sub = 0; seen = 0
def flush():
    if name is not None: frame[name] = pushed * 4 + sub
for line in text.split('\n'):
    m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
    if m:
        flush(); name = m.group(1); pushed = 0; sub = 0; seen = 0; calls[name] = set(); continue
    if name is None: continue
    if seen <= 12:
        m = re.search(r'\t(push|push\.w|stmdb)\s+(?:sp!, )?\{([^}]*)\}', line)
        if m and not pushed: pushed = len([r for r in m.group(2).split(',') if r.strip()])
        m = re.search(r'\tsub(?:w|\.w)?\s+sp, (?:sp, )?#(\d+)', line)
        if m and not sub: sub = int(m.group(1))
        seen += 1
    m = re.search(r'\t(?:bl|b\.w|b)\s+[0-9a-f]+ <([^>+]+)>', line)
    if m and m.group(1) != name: calls[name].add(m.group(1))
flush()
memo = {}
def depth(fn, stack=()):
    if fn in memo: return memo[fn]
    if fn in stack or fn not in frame: return (0, [fn])
    best = (0, [])
    for c in calls.get(fn, ()):
        d = depth(c, stack + (fn,))
        if d[0] > best[0]: best = d
    r = (frame[fn] + best[0], [fn] + best[1])
    memo[fn] = r
    return r
res = {}
for root in open(roots_file).read().split():
    if root in frame:
        d, chain = depth(root)
        res[root] = {'bytes': d, 'chain': chain[:14], 'frames': [frame[c] for c in chain[:14] if c in frame]}
    else:
        res[root] = 'not found'
json.dump(res, open(out, 'w'), indent=1)
for r, v in res.items():
    print(r, v if isinstance(v, str) else v['bytes'])
