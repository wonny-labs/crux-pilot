"""The UART report of the RT1060-EVKB bench image, and the host script that judges it (C2).

The C side (report/bench_report_format.c, report/bench_report_policy.c) is built here with the host C compiler and run:
its own checks, and a sample report that this test then feeds to check_bench_uart.py, so that the parser reads what the
C code wrote. The checker is also run over hand-made captures: a good one, and one broken at a time (a stack under the
margin, a torn report, a lost line, a wrong size, a missing HTTP row, a different certificate). Nothing here touches a
board and nothing here measures one: the numbers in the captures are made up to exercise the rules.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tests/zephyr/management_rt1060_compile"
CHECKER = BENCH / "check_bench_uart.py"
# Importing the checker must not leave a __pycache__ in the image's folder: test_management_bench_declaration.py reads
# every file there as text, and a bytecode file is not text.
sys.dont_write_bytecode = True
sys.path.insert(0, str(BENCH))
import check_bench_uart as checker  # noqa: E402

FINGERPRINT = ":".join(f"{b:02X}" for b in hashlib.sha256(b"a certificate").digest())
OTHER = ":".join(f"{b:02X}" for b in hashlib.sha256(b"another certificate").digest())


def stack(seq, name, size, unused, state="live"):
    return f"BENCH-STACK seq={seq} thread={name} size={size} used={size - unused} unused={unused} state={state}"


POOL_SIZES = {"rx_pkt": 192, "tx_pkt": 32, "rx_buf": 1024, "tx_buf": 768}


def pool_rows(seq, peaks=None, sizes=None, omit=(), net="ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=0 tcp_conndrop=0"):
    """The BENCH-POOL lines (row C6) and the BENCH-NET line of a report, as the image prints them after BENCH-HEAP."""
    sizes = {**POOL_SIZES, **(sizes or {})}
    peaks = {"rx_pkt": 20, "tx_pkt": 6, "rx_buf": 150, "tx_buf": 90, **(peaks or {})}
    rows = [f"BENCH-POOL seq={seq} pool={name} size={sizes[name]} used={min(3, peaks[name])} peak={peaks[name]}"
            for name in POOL_SIZES if name not in omit]
    if net is not None:
        rows.append(f"BENCH-NET seq={seq} {net}")
    return rows


def report(seq, reason="quiet", http_unused=5000, worker_unused=6000, heap_peak=100000, missed=0, extra=(),
           pools=None, main_unused=2000):
    rows = [stack(seq, "main", 16384, main_unused, "ended"), stack(seq, "http_server_tid", 8192, http_unused),
            stack(seq, "ssh_listener", 4096, 2900), stack(seq, "ssh_worker", 16384, worker_unused),
            stack(seq, "bench_report", 2048, 1400), *extra]
    heap = f"BENCH-HEAP seq={seq} arena=524288 in_use=90000 peak={heap_peak} free=300000"
    pool = pool_rows(seq, **pools) if pools is not None else []
    lines = len(rows) + 1 + len(pool)
    return "\n".join([f"BENCH-REPORT v=1 begin seq={seq} reason={reason} uptime_ms={seq * 1000} bursts={seq - 1}",
                      *rows, heap, *pool, f"BENCH-REPORT v=1 end seq={seq} lines={lines} missed={missed}"]) + "\n"


def capture(*, cert=FINGERPRINT, reports=None):
    text = "BENCH image=crux-mgmt-bench-rt1060-evkb-b declaration=CRUX_MANAGEMENT_BENCH_IMAGE\n"
    text += f"BENCH-CERT v=1 sha256={cert} source=generated\n" if cert else ""
    text += "\n".join(reports if reports is not None else [report(1, "boot"), report(2, "quiet"), report(3, "idle")])
    return text


def run_checker(text, *args):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "uart.log"
        path.write_text(text)
        return subprocess.run([sys.executable, "-I", str(CHECKER), str(path), *args], capture_output=True, text=True,
                              check=False)


class CCode(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("cc")
        if cls.compiler is None:
            raise unittest.SkipTest("no host C compiler")
        cls.folder = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.folder.name) / "bench_report_host_test"
        sources = [BENCH / "report/bench_report_host_test.c", BENCH / "report/bench_report_format.c",
                   BENCH / "report/bench_report_policy.c"]
        build = subprocess.run([cls.compiler, "-std=c17", "-Wall", "-Wextra", "-Werror", f"-I{BENCH / 'report'}",
                                *map(str, sources), "-o", str(cls.binary)], capture_output=True, text=True, check=False)
        if build.returncode != 0:
            raise AssertionError(build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def test_c_checks_pass(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("all checks passed", result.stdout)

    def test_checker_reads_what_the_c_code_writes(self):
        sample = subprocess.run([str(self.binary), "--sample"], capture_output=True, text=True, check=False)
        self.assertEqual(sample.returncode, 0)
        result = run_checker(sample.stdout, "--expect-http-stack-bytes", "8192")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("2 reports", result.stdout)
        # The sample has 4608 of 8192 B unused at the lowest (4598 after the 10 B step), about 56 percent.
        self.assertIn("http_server_tid", result.stdout)
        # Row C6: the sample holds the pool and counter lines that the image prints, and the checker judged them.
        self.assertIn("BENCH-POOL seq=1 pool=rx_buf", sample.stdout)
        self.assertIn("BENCH-NET seq=2", sample.stdout)
        self.assertIn("rx_buf", result.stdout)


class Grammar(unittest.TestCase):
    def test_a_good_capture_passes(self):
        result = run_checker(capture(), "--expect-http-stack-bytes", "8192")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_log_prefixes_and_noise_are_tolerated(self):
        noisy = "\n".join(f"[00:00:01.000,000] <inf> x: {line}" if i % 3 == 0 else line
                          for i, line in enumerate(capture().splitlines())) + "\r\n"
        noisy = noisy.replace("\n", "\r\n")
        noisy += "garbage \x00\xff line\r\n"
        self.assertEqual(run_checker(noisy).returncode, 0)

    def test_the_http_row_under_the_margin_fails(self):
        # 8192 B stack, 25 percent is 2048 B: 2047 B unused fails and 2048 B passes.
        failing = run_checker(capture(reports=[report(1, "boot"), report(2, "quiet", http_unused=2047)]))
        self.assertEqual(failing.returncode, 1, failing.stdout)
        self.assertIn("http_server_tid", failing.stdout)
        self.assertIn("FAIL", failing.stdout)
        passing = run_checker(capture(reports=[report(1, "boot"), report(2, "quiet", http_unused=2048)]))
        self.assertEqual(passing.returncode, 0, passing.stdout)

    def test_the_lowest_row_over_all_reports_counts(self):
        text = capture(reports=[report(1, "boot", http_unused=7000), report(2, "quiet", http_unused=1000),
                                report(3, "idle", http_unused=7000)])
        self.assertEqual(run_checker(text).returncode, 1)

    def test_the_margin_is_a_parameter(self):
        text = capture(reports=[report(1, "boot"), report(2, "quiet", http_unused=3000)])
        self.assertEqual(run_checker(text, "--http-margin-percent", "50").returncode, 1)
        self.assertEqual(run_checker(text, "--http-margin-percent", "25").returncode, 0)

    def test_other_threads_warn_and_fail_only_when_strict(self):
        text = capture(reports=[report(1, "boot"), report(2, "quiet", worker_unused=100)])
        warned = run_checker(text)
        self.assertEqual(warned.returncode, 0, warned.stdout)
        self.assertIn("WARN", warned.stdout)
        self.assertIn("ssh_worker", warned.stdout)
        self.assertEqual(run_checker(text, "--strict").returncode, 1)
        self.assertEqual(run_checker(text, "--fail-thread", "ssh_worker").returncode, 1)

    def test_two_threads_with_one_name_and_two_sizes_are_judged_apart(self):
        def extra(seq):
            return [stack(seq, "wq", 1024, 800), stack(seq, "wq", 4096, 3000)]
        text = capture(reports=[report(1, "boot", extra=extra(1)), report(2, "quiet", extra=extra(2))])
        result = run_checker(text, "--json")
        self.assertEqual(result.returncode, 0, result.stdout)
        summary = json.loads(result.stdout)
        self.assertEqual({k: v["size"] for k, v in summary["threads"].items() if k.startswith("wq")},
                         {"wq/1024": 1024, "wq/4096": 4096})
        self.assertTrue(any("different sizes" in w for w in summary["warnings"]))

    def test_the_http_thread_with_two_sizes_fails(self):
        def extra(seq):
            return [stack(seq, "http_server_tid", 4096, 3000)]
        result = run_checker(capture(reports=[report(1, "boot", extra=extra(1)), report(2, "quiet", extra=extra(2))]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("different sizes", result.stdout)

    def test_missing_http_row_fails(self):
        rows = capture().replace("http_server_tid", "other_thread")
        result = run_checker(rows)
        self.assertEqual(result.returncode, 1)
        self.assertIn("no row for http_server_tid", result.stdout)

    def test_a_row_that_does_not_add_up_fails(self):
        bad = capture().replace("size=8192 used=3192 unused=5000", "size=8192 used=3000 unused=5000", 1)
        self.assertNotEqual(bad, capture())
        self.assertEqual(run_checker(bad).returncode, 1)

    def test_size_must_match_the_declared_stack(self):
        result = run_checker(capture(), "--expect-http-stack-bytes", "4096")
        self.assertEqual(result.returncode, 1)
        self.assertIn("size", result.stdout)

    def test_size_comes_from_the_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder) / "bench-manifest.json"
            manifest.write_text(json.dumps({"settings": {"CONFIG_HTTP_SERVER_STACK_SIZE": "8192",
                                                          "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE": "524288"}}))
            self.assertEqual(run_checker(capture(), "--manifest", str(manifest)).returncode, 0)
            manifest.write_text(json.dumps({"settings": {"CONFIG_HTTP_SERVER_STACK_SIZE": "16384"}}))
            self.assertEqual(run_checker(capture(), "--manifest", str(manifest)).returncode, 1)

    def test_torn_report_fails(self):
        torn = report(2, "quiet").replace("BENCH-HEAP", "garbled", 1)
        result = run_checker(capture(reports=[report(1, "boot"), torn]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("heap", result.stdout)

    def test_report_without_end_fails(self):
        lines = report(2, "quiet").splitlines()
        text = capture(reports=[report(1, "boot"), "\n".join(lines[:-1]) + "\n", report(3, "idle")])
        self.assertEqual(run_checker(text).returncode, 1)

    def test_a_lost_line_fails(self):
        lines = report(2, "quiet").splitlines()
        del lines[2]
        result = run_checker(capture(reports=[report(1, "boot"), "\n".join(lines) + "\n"]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("lines", result.stdout)

    def test_a_gap_in_the_sequence_fails(self):
        result = run_checker(capture(reports=[report(1, "boot"), report(3, "quiet")]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("seq", result.stdout)

    def test_missed_threads_fail(self):
        result = run_checker(capture(reports=[report(1, "boot"), report(2, "quiet", missed=2)]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("missed", result.stdout)

    def test_too_long_line_fails(self):
        long = capture().replace("thread=main", "thread=" + "m" * 200, 1)
        result = run_checker(long)
        self.assertEqual(result.returncode, 1)
        self.assertIn("over the limit", result.stdout)

    def test_needs_a_boot_report_and_a_client_report(self):
        self.assertEqual(run_checker(capture(reports=[report(1, "boot")])).returncode, 1)
        self.assertEqual(run_checker(capture(reports=[report(1, "boot")]), "--min-client-reports", "0").returncode, 0)
        self.assertEqual(run_checker(capture(reports=[report(2, "quiet")])).returncode, 1)

    def test_heap_peak_limit(self):
        text = capture(reports=[report(1, "boot"), report(2, "quiet", heap_peak=450000)])
        result = run_checker(text)
        self.assertEqual(result.returncode, 1)
        self.assertIn("heap", result.stdout)
        self.assertEqual(run_checker(text, "--heap-peak-limit-percent", "90").returncode, 0)

    def test_arena_must_match_the_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder) / "bench-manifest.json"
            manifest.write_text(json.dumps({"settings": {"CONFIG_HTTP_SERVER_STACK_SIZE": "8192",
                                                          "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE": "1048576"}}))
            self.assertEqual(run_checker(capture(), "--manifest", str(manifest)).returncode, 1)

    def test_json_summary(self):
        result = run_checker(capture(), "--json")
        self.assertEqual(result.returncode, 0, result.stdout)
        summary = json.loads(result.stdout)
        self.assertEqual(summary["verdict"], "PASS")
        self.assertEqual(summary["reports"], 3)
        http = summary["threads"]["http_server_tid"]
        self.assertEqual((http["size"], http["unused_min"], http["used_max"]), (8192, 5000, 3192))
        self.assertEqual(summary["heap"]["peak_max"], 100000)
        self.assertEqual(summary["certificate"], FINGERPRINT)


def manifest_with(folder, **settings):
    path = Path(folder) / "bench-manifest.json"
    path.write_text(json.dumps({"settings": {"CONFIG_HTTP_SERVER_STACK_SIZE": "8192",
                                              "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE": "524288", **settings}}))
    return path


MANIFEST_POOLS = {"CONFIG_NET_PKT_RX_COUNT": "192", "CONFIG_NET_PKT_TX_COUNT": "32", "CONFIG_NET_BUF_RX_COUNT": "1024",
                  "CONFIG_NET_BUF_TX_COUNT": "768"}


class Pools(unittest.TestCase):
    """Row C6: the network buffer pools' high-water marks and the TCP counters, which a board run reads to confirm or
    change the sizes of docs/rt1060-bench-network-buffers.md. The numbers here are made up to exercise the rules."""

    def reports(self, **pools):
        return capture(reports=[report(1, "boot", pools=pools), report(2, "quiet", pools=pools)])

    def test_a_capture_with_pools_passes_and_the_summary_has_them(self):
        result = run_checker(self.reports(), "--json")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual(summary["pools"]["rx_buf"], {"size": 1024, "peak_max": 150, "peak_percent": 150 / 1024 * 100,
                                                      "limit_percent": 80})
        self.assertEqual(set(summary["pools"]), set(POOL_SIZES))
        self.assertEqual(summary["net"]["tcp_rexmit"], 0)
        human = run_checker(self.reports())
        self.assertIn("rx_buf", human.stdout)

    def test_a_pool_peak_over_the_limit_fails_and_the_limit_is_a_parameter(self):
        # 80 percent of 1024 is 819.2: 819 passes and 820 fails.
        self.assertEqual(run_checker(self.reports(peaks={"rx_buf": 819})).returncode, 0)
        over = run_checker(self.reports(peaks={"rx_buf": 820}))
        self.assertEqual(over.returncode, 1, over.stdout)
        self.assertIn("rx_buf", over.stdout)
        self.assertEqual(run_checker(self.reports(peaks={"rx_buf": 820}), "--pool-peak-limit-percent", "90").returncode, 0)
        for name in POOL_SIZES:
            full = run_checker(self.reports(peaks={name: POOL_SIZES[name]}))
            self.assertEqual(full.returncode, 1, f"{name}: {full.stdout}")
            self.assertIn(name, full.stdout)

    def test_the_pool_limit_is_exact_where_it_is_a_whole_number(self):
        # 50 percent of 1024 is 512: at the limit passes, one over fails (80 percent of these sizes is never whole).
        self.assertEqual(run_checker(self.reports(peaks={"rx_buf": 512}), "--pool-peak-limit-percent", "50").returncode, 0)
        self.assertEqual(run_checker(self.reports(peaks={"rx_buf": 513}), "--pool-peak-limit-percent", "50").returncode, 1)

    def test_a_pool_that_changes_size_between_reports_fails(self):
        text = capture(reports=[report(1, "boot", pools={}), report(2, "quiet", pools={"sizes": {"rx_buf": 2048}})])
        result = run_checker(text)
        self.assertEqual(result.returncode, 1)
        self.assertIn("different sizes", result.stdout)

    def test_two_net_lines_in_a_report_fail(self):
        doubled = report(1, "boot", pools={}).replace(
            "BENCH-REPORT v=1 end", "BENCH-NET seq=1 ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=0 tcp_conndrop=0\n"
            "BENCH-REPORT v=1 end").replace("lines=11", "lines=12")
        self.assertIn("lines=12", doubled)
        result = run_checker(capture(reports=[doubled]), "--min-client-reports", "0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("at most 1", result.stdout)

    def test_the_lowest_pool_headroom_over_all_reports_counts(self):
        text = capture(reports=[report(1, "boot", pools={}), report(2, "quiet", pools={"peaks": {"tx_buf": 700}}),
                                report(3, "idle", pools={})])
        failed = run_checker(text)
        self.assertEqual(failed.returncode, 1)
        self.assertIn("tx_buf", failed.stdout)

    def test_pool_sizes_must_match_the_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = manifest_with(folder, **MANIFEST_POOLS)
            self.assertEqual(run_checker(self.reports(), "--manifest", str(manifest)).returncode, 0)
            for symbol in MANIFEST_POOLS:
                stale = manifest_with(folder, **{**MANIFEST_POOLS, symbol: str(int(MANIFEST_POOLS[symbol]) + 1)})
                result = run_checker(self.reports(), "--manifest", str(stale))
                self.assertEqual(result.returncode, 1, f"{symbol}: {result.stdout}")
                self.assertIn("declares", result.stdout)

    def test_a_manifest_that_declares_pools_needs_pool_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = manifest_with(folder, **MANIFEST_POOLS)
            result = run_checker(capture(), "--manifest", str(manifest))
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("BENCH-POOL", result.stdout)
        # Without a manifest and without the flag, a capture of the image before row C6 is still judged as before.
        self.assertEqual(run_checker(capture()).returncode, 0)
        self.assertEqual(run_checker(capture(), "--require-pools").returncode, 1)

    def test_all_four_pools_once_per_report(self):
        for name in POOL_SIZES:
            short = capture(reports=[report(1, "boot", pools={"omit": (name,)})])
            result = run_checker(short, "--min-client-reports", "0")
            self.assertEqual(result.returncode, 1, f"{name}: {result.stdout}")
            self.assertIn(name, result.stdout)
        twice = report(1, "boot", pools={}).replace("BENCH-NET", "BENCH-POOL seq=1 pool=rx_pkt size=192 used=1 peak=2\nBENCH-NET")
        # The end line is made to count the repeat, so only the rule against a second row can refuse the report.
        twice = twice.replace("lines=11", "lines=12")
        self.assertIn("lines=12", twice)
        result = run_checker(capture(reports=[twice]), "--min-client-reports", "0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("second row for pool rx_pkt", result.stdout)

    def test_a_pool_row_that_does_not_add_up_fails(self):
        # A use over the size with a peak that is fine, and a peak over the size: neither may pass on the other's rule.
        for bad in ("size=192 used=193 peak=20", "size=192 used=1 peak=193", "size=192 used=193 peak=193"):
            text = capture(reports=[report(1, "boot", pools={})]).replace("size=192 used=3 peak=20", bad, 1)
            self.assertNotEqual(text, capture(reports=[report(1, "boot", pools={})]))
            result = run_checker(text, "--min-client-reports", "0")
            self.assertEqual(result.returncode, 1, f"{bad}: {result.stdout}")

    def test_the_end_line_counts_pool_and_net_lines(self):
        lines = report(2, "quiet", pools={}).splitlines()
        del lines[-3]  # one pool line is lost
        result = run_checker(capture(reports=[report(1, "boot", pools={}), "\n".join(lines) + "\n"]))
        self.assertEqual(result.returncode, 1)
        self.assertIn("lines", result.stdout)

    def test_counters_warn_and_fail_only_when_strict(self):
        # main has room here, so --strict has nothing to fail on but the counter.
        clean = capture(reports=[report(1, "boot", pools={}, main_unused=9000),
                                 report(2, "quiet", pools={}, main_unused=9000)])
        self.assertEqual(run_checker(clean, "--strict").returncode, 0)
        for counter in ("ip_drop", "tcp_drop", "tcp_seg_drop", "tcp_rexmit", "tcp_conndrop"):
            net = " ".join(f"{name}={3 if name == counter else 0}"
                           for name in ("ip_drop", "tcp_drop", "tcp_seg_drop", "tcp_rexmit", "tcp_conndrop"))
            text = capture(reports=[report(1, "boot", pools={}, main_unused=9000),
                                    report(2, "quiet", pools={"net": net}, main_unused=9000)])
            warned = run_checker(text)
            self.assertEqual(warned.returncode, 0, f"{counter}: {warned.stdout}")
            self.assertIn(f"WARN {counter}", warned.stdout)
            self.assertEqual(run_checker(text, "--strict").returncode, 1, counter)

    def test_the_counters_are_cumulative_so_the_last_report_is_read(self):
        text = capture(reports=[report(1, "boot", pools={"net": "ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=0 tcp_conndrop=0"}),
                                report(2, "quiet", pools={"net": "ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=7 tcp_conndrop=0"}),
                                report(3, "idle", pools={"net": "ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=7 tcp_conndrop=0"})])
        result = run_checker(text, "--json")
        self.assertEqual(json.loads(result.stdout)["net"]["tcp_rexmit"], 7)

    def test_pool_rows_and_the_net_line_come_together(self):
        for drop, lines in (("BENCH-NET", "lines=10"), ("BENCH-POOL", "lines=7")):
            kept = [line for line in report(1, "boot", pools={}).splitlines() if not line.startswith(drop)]
            text = "\n".join(kept).replace("lines=11", lines) + "\n"
            result = run_checker(capture(reports=[text]), "--min-client-reports", "0")
            self.assertEqual(result.returncode, 1, f"{drop}: {result.stdout}")
            self.assertIn("come together", result.stdout)

    def test_every_report_holds_pools_once_one_does(self):
        text = capture(reports=[report(1, "boot"), report(2, "quiet", pools={})])
        result = run_checker(text)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("no BENCH-POOL rows", result.stdout)
        with tempfile.TemporaryDirectory() as folder:
            manifest = manifest_with(folder, **MANIFEST_POOLS)
            partial = capture(reports=[report(1, "boot", pools={}), report(2, "quiet")])
            self.assertEqual(run_checker(partial, "--manifest", str(manifest)).returncode, 1)

    def test_the_last_report_is_the_one_read_even_when_a_counter_is_lower(self):
        # Counters grow on a board; the judge reads the last report, not the highest value.
        text = capture(reports=[report(1, "boot", pools={"net": "ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=7 tcp_conndrop=0"}),
                                report(2, "quiet", pools={"net": "ip_drop=0 tcp_drop=0 tcp_seg_drop=0 tcp_rexmit=0 tcp_conndrop=0"})])
        self.assertEqual(json.loads(run_checker(text, "--json").stdout)["net"]["tcp_rexmit"], 0)

    def test_a_malformed_net_line_fails(self):
        text = capture(reports=[report(1, "boot", pools={})]).replace("tcp_rexmit=0", "tcp_rexmit=x", 1)
        self.assertEqual(run_checker(text, "--min-client-reports", "0").returncode, 1)


class NativeTestcase(unittest.TestCase):
    """tests/native/bench_report/testcase.yaml is what Twister reads: its ordered regexes must match the run that the
    evidence record holds, in that order, one line each, the way the multi_line console harness walks the output."""

    def test_ordered_regexes_match_the_recorded_run_in_order(self):
        import re
        # Row C6 added the pool and counter lines to the scenario, so the run that matches is the one recorded with it
        # (the run that C2 recorded has none of them and stays unchanged as C2's evidence).
        evidence = ROOT / "docs/evidence/rt1060-management-image-feasibility/network-buffers-c6/native/run-no-rt.txt"
        patterns = [m.group(1) for m in re.finditer(r"^\s+- '(.+)'$",
                                                    (ROOT / "tests/native/bench_report/testcase.yaml").read_text(),
                                                    re.MULTILINE)]
        self.assertGreater(len(patterns), 10)
        lines = evidence.read_text().splitlines()
        at = 0
        for pattern in patterns:
            while at < len(lines) and not re.search(pattern, lines[at]):
                at += 1
            self.assertLess(at, len(lines), f"not found, in order: {pattern}")
            at += 1


    def test_the_recorded_run_has_pool_lines_in_three_of_four_reports_and_counts_them(self):
        # The fake read of tests/native/bench_report/src/main.c fails the third time: that report has no pool and no
        # counter lines, and its end line must say so. The checker's own line count is what catches a wrong count.
        evidence = ROOT / "docs/evidence/rt1060-management-image-feasibility/network-buffers-c6/native/run-no-rt.txt"
        parsed = checker.Capture(evidence.read_text())
        self.assertEqual([len(r["pool"]) for r in parsed.reports], [4, 4, 0, 4])
        self.assertEqual([len(r["net"]) for r in parsed.reports], [1, 1, 0, 1])
        self.assertEqual([m for m in parsed.failures if "missed" not in m], [])
        self.assertEqual([r["net"][0]["tcp_rexmit"] for r in parsed.reports if r["net"]], [0, 1, 3])


class Certificate(unittest.TestCase):
    def test_the_fingerprint_must_be_there_when_compared(self):
        result = run_checker(capture(cert=None), "--cert-fingerprint", FINGERPRINT)
        self.assertEqual(result.returncode, 1)
        self.assertIn("BENCH-CERT", result.stdout)

    def test_matching_and_different_fingerprints(self):
        self.assertEqual(run_checker(capture(), "--cert-fingerprint", FINGERPRINT).returncode, 0)
        # A tool's lower case or its label is accepted.
        self.assertEqual(run_checker(capture(), "--cert-fingerprint", FINGERPRINT.lower()).returncode, 0)
        different = run_checker(capture(), "--cert-fingerprint", OTHER)
        self.assertEqual(different.returncode, 1)
        self.assertIn("differs", different.stdout)

    def test_a_malformed_fingerprint_line_fails(self):
        bad = capture(cert=FINGERPRINT[:-3])
        self.assertEqual(run_checker(bad).returncode, 1)

    def test_the_last_boot_is_the_one_compared(self):
        text = (f"BENCH-CERT v=1 sha256={OTHER} source=generated\n" + capture())
        self.assertEqual(run_checker(text, "--cert-fingerprint", FINGERPRINT).returncode, 0)
        self.assertEqual(run_checker(text, "--cert-fingerprint", OTHER).returncode, 1)

    def test_openssl_output_forms(self):
        for text in (f"sha256 Fingerprint={FINGERPRINT}\n", f"SHA256 Fingerprint={FINGERPRINT}\n",
                     f"SHA256 Fingerprint={FINGERPRINT.lower()}"):
            self.assertEqual(checker.fingerprint_from_openssl_output(text), FINGERPRINT)
        with self.assertRaises(checker.CheckError):
            checker.fingerprint_from_openssl_output("sha1 Fingerprint=" + FINGERPRINT[:59])
        with self.assertRaises(checker.CheckError):
            checker.fingerprint_from_openssl_output("")

    @unittest.skipUnless(shutil.which("openssl"), "no openssl")
    def test_a_real_certificate_through_openssl(self):
        with tempfile.TemporaryDirectory() as folder:
            key, pem = Path(folder) / "key.pem", Path(folder) / "cert.pem"
            made = subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
                                   "-nodes", "-keyout", str(key), "-out", str(pem), "-days", "1", "-subj", "/CN=bench"],
                                  capture_output=True, text=True, check=False)
            if made.returncode != 0:
                self.skipTest("openssl could not make a certificate: " + made.stderr)
            expected = checker.fingerprint_from_pem(pem)
            self.assertRegex(expected, r"^([0-9A-F]{2}:){31}[0-9A-F]{2}$")
            self.assertEqual(run_checker(capture(cert=expected), "--cert-pem", str(pem)).returncode, 0)
            self.assertEqual(run_checker(capture(cert=OTHER), "--cert-pem", str(pem)).returncode, 1)


if __name__ == "__main__":
    unittest.main()
