# Static worst-case stack along direct calls, as commands/callgraph.py of the stage 0 evidence does it, with one change:
# a function is identified by its address and not by its name. Two static functions with one name (wolfSSL's global
# ReceiveData and wolfSSH's static ReceiveData are one example) are then two nodes, where the name-keyed script merges
# them and may follow a call that the real code never makes. The frame of a function is its pushed registers plus its
# first "sub sp"; a tail branch (b, b.w) counts as a nested call; a call through a function pointer is not followed;
# recursion is cut at the first repeat. A root named on the command line is every function with that name, and the
# deepest chain of them is reported.
# usage: callgraph_by_address.py ELF ROOTS_FILE OUT_JSON [OBJDUMP]
import re, subprocess, sys, json
elf, roots_file, out = sys.argv[1], sys.argv[2], sys.argv[3]
OBJ = sys.argv[4] if len(sys.argv) > 4 else '/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump'
text = subprocess.run([OBJ, '-d', '--no-show-raw-insn', elf], capture_output=True, text=True).stdout
frame = {}; calls = {}; label = {}; by_name = {}
node = None; pushed = 0; sub = 0; seen = 0
def flush():
    if node is not None: frame[node] = pushed * 4 + sub
for line in text.split('\n'):
    m = re.match(r'^([0-9a-f]+) <(.+)>:$', line)
    if m:
        flush(); node = int(m.group(1), 16); label[node] = m.group(2); by_name.setdefault(m.group(2), []).append(node)
        pushed = 0; sub = 0; seen = 0; calls[node] = set(); continue
    if node is None: continue
    if seen <= 12:
        m = re.search(r'\t(push|push\.w|stmdb)\s+(?:sp!, )?\{([^}]*)\}', line)
        if m and not pushed: pushed = len([r for r in m.group(2).split(',') if r.strip()])
        m = re.search(r'\tsub(?:w|\.w)?\s+sp, (?:sp, )?#(\d+)', line)
        if m and not sub: sub = int(m.group(1))
        seen += 1
    m = re.search(r'\t(?:bl|b\.w|b)\s+([0-9a-f]+) <([^>+]+)>', line)
    if m:
        target = int(m.group(1), 16)
        if target != node: calls[node].add(target)
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
    nodes = by_name.get(root)
    if not nodes:
        res[root] = 'not found'; continue
    d, chain = max((depth(n) for n in nodes), key=lambda x: x[0])
    res[root] = {'bytes': d, 'chain': [f"{label.get(c, hex(c))}@{c:x}" for c in chain[:14]],
                 'frames': [frame[c] for c in chain[:14] if c in frame], 'functions_with_this_name': len(nodes)}
json.dump(res, open(out, 'w'), indent=1)
for r, v in res.items():
    print(r, v if isinstance(v, str) else v['bytes'])
