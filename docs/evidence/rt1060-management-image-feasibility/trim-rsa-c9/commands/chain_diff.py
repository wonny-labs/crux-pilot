#!/usr/bin/env python3
"""Static stack chains of two images side by side. Host script, reads the JSON of callgraph.py and
callgraph_by_address.py.  usage: chain_diff.py BEFORE-by-name.json AFTER-by-name.json AFTER-by-address.json"""
import json
import sys

before, after, after_addr = (json.load(open(p)) for p in sys.argv[1:4])
print(f"{'root':38} {'before':>7} {'after':>7} {'diff':>7} {'after by address':>17}")
for root, b in before.items():
    a, c = after.get(root), after_addr.get(root)
    if isinstance(b, str) or isinstance(a, str):
        print(f"{root:38} {b if isinstance(b, str) else b['bytes']:>7} {a if isinstance(a, str) else a['bytes']:>7}")
        continue
    print(f"{root:38} {b['bytes']:7d} {a['bytes']:7d} {a['bytes'] - b['bytes']:+7d} {c['bytes']:17d}")
print()
for root in ("main", "web_session_work_once", "crux_access_login_password", "SendTls13CertificateVerify", "wolfSSH_worker",
             "wolfSSL_accept"):
    for label, data in (("before", before), ("after (name)", after), ("after (address)", after_addr)):
        print(f"{root} {label}: {data[root]['bytes']} B")
        print("   " + " > ".join(data[root]["chain"]))
