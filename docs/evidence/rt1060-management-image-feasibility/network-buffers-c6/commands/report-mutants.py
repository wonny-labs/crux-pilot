# Mutants of the row C6 additions to the UART report: check_bench_uart.py (the pool and counter rules) and
# report/bench_report_format.c (the BENCH-POOL and BENCH-NET lines). Each weakens one rule and a test must then fail:
# tests/build/test_management_bench_report.py for the checker, and the host C test (built with the address and
# undefined-behaviour sanitizers) for the formatter. Run from the repository root. Prints one line per mutant.
import shutil
import subprocess
import tempfile

V = "tests/zephyr/management_rt1060_compile/"
CHECKER = V + "check_bench_uart.py"
FORMAT = V + "report/bench_report_format.c"

CHECKER_MUTANTS = [
    ('if entry["peak_max"] * 100 > entry["size"] * options.pool_peak_limit_percent:', "if False:", "pool peak limit ignored"),
    ('if entry["peak_max"] * 100 > entry["size"] * options.pool_peak_limit_percent:',
     'if entry["peak_max"] * 100 >= entry["size"] * options.pool_peak_limit_percent:', "pool limit compared with >= (off by one)"),
    ('entry["peak_max"] = max(entry["peak_max"], row["peak"])', "pass", "pool peak not kept over reports"),
    ('if name in expected_pools and entry["size"] != expected_pools[name]:', "if False:", "pool size not compared with the build"),
    ('if len(entry["sizes"]) > 1:', "if False:", "a pool that changes size is accepted"),
    ('if current["pool"] and {row["name"] for row in current["pool"]} != set(POOL_SYMBOLS):', "if False:",
     "a report with a pool missing is accepted"),
    ('if used > size or peak > size:', "if False:", "a pool row over its size is accepted"),
    ('if any(row["name"] == name for row in current["pool"]):', "if False:", "a second row for one pool is accepted"),
    ('(failures if options.strict else warnings).append(', "warnings.append(", "--strict does not fail a non-zero counter"),
    ('(failures if options.strict else warnings).append(', "failures.append(", "a non-zero counter always fails"),
    ("net = counters[-1] if counters else None", "net = counters[0] if counters else None", "first report read for the counters"),
    ('if options.require_pools or expected_pools:', "if False:", "missing pool lines accepted"),
    ('+ len(current["pool"]) + len(current["net"])', '+ len(current["net"])', "pool lines not counted in the end line"),
    ('if len(current["net"]) > 1:', "if False:", "two BENCH-NET lines accepted"),
    ('if name in expected_pools and entry["size"]', 'if name in expected_pools and False and entry["size"]', "declared size skipped"),
    ('if bool(current["pool"]) != bool(current["net"]):', "if False:", "pool rows without a net line (or the reverse) accepted"),
    ('if not report["pool"]:', "if False:", "a report without pool rows accepted among reports that have them"),
    ("net = counters[-1] if counters else None", 'net = max(counters, key=lambda c: c["tcp_rexmit"]) if counters else None',
     "the highest counter read instead of the last report's"),
]

FORMAT_MUTANTS = [
    ("if (!pool_name_valid(pool) || used > size || peak > size) {", "if (!pool_name_valid(pool) || used > size) {",
     "a peak over the size is accepted"),
    ("if (!pool_name_valid(pool) || used > size || peak > size) {", "if (!pool_name_valid(pool) || peak > size) {",
     "a use over the size is accepted"),
    ("if (!pool_name_valid(pool) || used > size || peak > size) {", "if (used > size || peak > size) {", "a bad name is accepted"),
    ("if (!((name[i] >= 'a' && name[i] <= 'z') || name[i] == '_') || i >= 15u) {",
     "if (!((name[i] >= 'A' && name[i] <= 'z') || name[i] == '_') || i >= 15u) {", "upper case names accepted"),
    ("if (!((name[i] >= 'a' && name[i] <= 'z') || name[i] == '_') || i >= 15u) {",
     "if (!((name[i] >= 'a' && name[i] <= 'z')) || i >= 15u) {", "an underscore is not part of a name"),
    ("if (!((name[i] >= 'a' && name[i] <= 'z') || name[i] == '_') || i >= 15u) {",
     "if (!((name[i] >= 'a' && name[i] <= 'z') || name[i] == '_')) {", "no length limit on a pool name"),
    ("if (!((name[i] >= 'a' && name[i] <= 'z') || name[i] == '_') || i >= 15u) {",
     "if (!((name[i] >= 'a' && name[i] <= 'z') || name[i] == '_' || name[i] == '-') || i >= 15u) {", "a hyphen is part of a name"),
    ('put_field(&w, "used", used);\n    put_field(&w, "peak", peak);',
     'put_field(&w, "used", used);\n    put_field(&w, "peak", used);', "peak printed as used"),
    ('put_field(&w, "used", used);\n    put_field(&w, "peak", peak);',
     'put_field(&w, "used", peak);\n    put_field(&w, "peak", peak);', "used printed as peak"),
    ('put_field(&w, "size", size);\n    put_field(&w, "used", used);',
     'put_field(&w, "size", used);\n    put_field(&w, "used", used);', "size printed as used"),
    ('put_field(&w, "tcp_rexmit", tcp_rexmit);', 'put_field(&w, "tcp_rexmit", tcp_drop);', "retransmissions printed as drops"),
    ('put_field(&w, "tcp_conndrop", tcp_conndrop);', 'put_field(&w, "tcp_conndrop", tcp_seg_drop);', "connection drops printed as segment drops"),
    ('put_field(&w, "ip_drop", ip_drop);', 'put_field(&w, "ip_drop", tcp_drop);', "IP drops printed as TCP drops"),
]


def run_checker_tests():
    return subprocess.run(["python3", "-m", "unittest", "tests/build/test_management_bench_report.py"],
                          capture_output=True, text=True).returncode


def main():
    # The control: the unmutated files must pass every test, or every mutant would be "killed" for that reason.
    if run_checker_tests() != 0:
        raise SystemExit("the control failed: the unmutated tests do not pass")
    print("control: the unmutated tests pass")
    results = []
    original = open(CHECKER).read()
    try:
        for old, new, description in CHECKER_MUTANTS:
            assert old in original, old
            open(CHECKER, "w").write(original.replace(old, new, 1))
            results.append(("killed" if run_checker_tests() != 0 else "SURVIVED", "checker: " + description))
    finally:
        open(CHECKER, "w").write(original)
    work = tempfile.mkdtemp()
    source = open(FORMAT).read()
    try:
        for old, new, description in FORMAT_MUTANTS:
            assert old in source, old
            open(FORMAT, "w").write(source.replace(old, new, 1))
            build = subprocess.run(["cc", "-std=c17", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-I" + V + "report",
                                    V + "report/bench_report_host_test.c", FORMAT, V + "report/bench_report_policy.c", "-o",
                                    work + "/mutant"], capture_output=True, text=True)
            if build.returncode != 0:
                results.append(("compile error", "formatter: " + description))
                continue
            sample = subprocess.run([work + "/mutant"], capture_output=True, text=True).returncode
            checker = run_checker_tests()
            # The C test and the Python tests that run its sample both look: either one failing is a kill.
            results.append(("killed" if sample != 0 or checker != 0 else "SURVIVED", "formatter: " + description))
    finally:
        open(FORMAT, "w").write(source)
        shutil.rmtree(work)
    for outcome, description in results:
        print(f"{outcome:14} {description}")
    print(sum(1 for outcome, _ in results if outcome == "killed"), "of", len(results), "killed")


main()
