import re, sys
from pathlib import Path
root = Path('.')
def slug(h):
    h = h.strip().lower()
    h = re.sub(r'`', '', h)
    h = re.sub(r'[^\w\- ]', '', h)
    return h.replace(' ', '-')
def anchors(path):
    out = set(); seen = {}
    for line in Path(path).read_text().splitlines():
        m = re.match(r'^(#{1,6})\s+(.*)$', line)
        if m:
            s = slug(m.group(2)); n = seen.get(s, 0); seen[s] = n + 1
            out.add(s if n == 0 else f"{s}-{n}")
    return out
bad = 0
for f in sys.argv[1:]:
    text = Path(f).read_text()
    for m in re.finditer(r'\]\(([^)\s]+)\)', text):
        target = m.group(1)
        if target.startswith(('http://', 'https://', 'mailto:')): continue
        path, _, frag = target.partition('#')
        dest = (Path(f).parent / path) if path else Path(f)
        if not dest.exists():
            print(f"{f}: missing {target}"); bad += 1; continue
        if frag and dest.suffix == '.md' and frag not in anchors(dest):
            print(f"{f}: no anchor {target}"); bad += 1
print("link problems:", bad)
