# What tests/build/test_management_bench_report.py asks of a run of tests/native/bench_report, for any capture of it: the
# ordered regexes of testcase.yaml, and the structure of the pool and counter lines. Prints PASS or the reasons.
# usage: python3 check-native-run.py RUN.txt      (from the repository root)
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, "tests/zephyr/management_rt1060_compile")
import check_bench_uart as checker  # noqa: E402

lines = Path(sys.argv[1]).read_text().splitlines()
reasons = []
patterns = [m.group(1) for m in re.finditer(r"^\s+- '(.+)'$", Path("tests/native/bench_report/testcase.yaml").read_text(), re.MULTILINE)]
at = 0
for pattern in patterns:
    while at < len(lines) and not re.search(pattern, lines[at]):
        at += 1
    if at >= len(lines):
        reasons.append(f"not found, in order: {pattern}")
        break
    at += 1
parsed = checker.Capture("\n".join(lines))
if [len(r["pool"]) for r in parsed.reports] != [4, 4, 0, 4]:
    reasons.append(f"pool rows per report {[len(r['pool']) for r in parsed.reports]}, expected [4, 4, 0, 4]")
other = [m for m in parsed.failures if "missed" not in m]
if other:
    reasons.append(f"the checker's own parse fails: {other[0]}")
print("PASS" if not reasons else "FAIL: " + "; ".join(reasons))
sys.exit(0 if not reasons else 1)
