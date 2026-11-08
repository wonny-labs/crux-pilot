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
sys.path.insert(0, str(BENCH))
import check_bench_uart as checker  # noqa: E402

FINGERPRINT = ":".join(f"{b:02X}" for b in hashlib.sha256(b"a certificate").digest())
OTHER = ":".join(f"{b:02X}" for b in hashlib.sha256(b"another certificate").digest())


def stack(seq, name, size, unused, state="live"):
    return f"BENCH-STACK seq={seq} thread={name} size={size} used={size - unused} unused={unused} state={state}"


def report(seq, reason="quiet", http_unused=5000, worker_unused=6000, heap_peak=100000, missed=0, extra=()):
    rows = [stack(seq, "main", 16384, 2000, "ended"), stack(seq, "http_server_tid", 8192, http_unused),
            stack(seq, "ssh_listener", 4096, 2900), stack(seq, "ssh_worker", 16384, worker_unused),
            stack(seq, "bench_report", 2048, 1400), *extra]
    heap = f"BENCH-HEAP seq={seq} arena=524288 in_use=90000 peak={heap_peak} free=300000"
    lines = len(rows) + 1
    return "\n".join([f"BENCH-REPORT v=1 begin seq={seq} reason={reason} uptime_ms={seq * 1000} bursts={seq - 1}",
                      *rows, heap, f"BENCH-REPORT v=1 end seq={seq} lines={lines} missed={missed}"]) + "\n"


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


class NativeTestcase(unittest.TestCase):
    """tests/native/bench_report/testcase.yaml is what Twister reads: its ordered regexes must match the run that the
    evidence record holds, in that order, one line each, the way the multi_line console harness walks the output."""

    def test_ordered_regexes_match_the_recorded_run_in_order(self):
        import re
        evidence = ROOT / "docs/evidence/rt1060-management-image-feasibility/bench-report-c2/native/run-no-rt.txt"
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
