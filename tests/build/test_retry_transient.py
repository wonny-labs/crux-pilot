"""Bounded retry of CI downloads: transient failures are retried, everything else fails at once."""
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock
import urllib.error

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts"))
import retry_transient as retry  # noqa: E402

SCRIPT = REPO / "scripts/retry_transient.py"


def http_error(code):
    return urllib.error.HTTPError("https://example.invalid/a", code, "status", None, io.BytesIO())


class FakeDownloader:
    """Raises the given errors in order, then returns 'payload'. Counts every call."""

    def __init__(self, *errors):
        self.errors = list(errors)
        self.calls = 0

    def __call__(self):
        self.calls += 1
        if self.errors:
            raise self.errors.pop(0)
        return "payload"


class RetryFunctionTest(unittest.TestCase):
    def run_retry(self, downloader, **options):
        sleeps = []
        output = io.StringIO()
        try:
            result = retry.retry(downloader, sleep=sleeps.append, log=output.write, **options)
        except Exception as error:  # noqa: BLE001 - the tests inspect what escaped
            return error, sleeps, output.getvalue()
        return result, sleeps, output.getvalue()

    def test_a_download_that_fails_twice_with_504_then_succeeds_is_returned(self):
        downloader = FakeDownloader(http_error(504), http_error(504))
        result, sleeps, output = self.run_retry(downloader, base_delay=5)
        self.assertEqual("payload", result)
        self.assertEqual(3, downloader.calls)
        self.assertEqual([5, 10], sleeps)
        self.assertIn("attempt 1/4 failed", output)
        self.assertIn("attempt 2/4 failed", output)

    def test_a_download_that_always_fails_stops_at_the_bound_and_says_so(self):
        downloader = FakeDownloader(*[http_error(503)] * 10)
        error, sleeps, output = self.run_retry(downloader, attempts=4, base_delay=1)
        self.assertIsInstance(error, urllib.error.HTTPError)
        self.assertEqual(4, downloader.calls)
        self.assertEqual(3, len(sleeps))
        self.assertIn("giving up after 4 attempts", output)

    def test_the_wait_doubles_but_never_exceeds_the_maximum(self):
        downloader = FakeDownloader(*[http_error(502)] * 10)
        _, sleeps, _ = self.run_retry(downloader, attempts=6, base_delay=10, max_delay=30)
        self.assertEqual([10, 20, 30, 30, 30], sleeps)

    def test_a_missing_file_is_not_retried(self):
        for code in (400, 403, 404, 429):
            with self.subTest(code=code):
                downloader = FakeDownloader(http_error(code))
                error, sleeps, _ = self.run_retry(downloader)
                self.assertIsInstance(error, urllib.error.HTTPError)
                self.assertEqual(1, downloader.calls)
                self.assertEqual([], sleeps)

    def test_a_checksum_mismatch_is_not_retried(self):
        downloader = FakeDownloader(ValueError("lychee release archive SHA-256 mismatch"))
        error, sleeps, _ = self.run_retry(downloader)
        self.assertIsInstance(error, ValueError)
        self.assertEqual(1, downloader.calls)
        self.assertEqual([], sleeps)

    def test_network_errors_and_timeouts_are_retried(self):
        for error in (urllib.error.URLError("connection reset"), TimeoutError("timed out"),
                      ConnectionResetError("reset"), http_error(500)):
            with self.subTest(error=type(error).__name__):
                downloader = FakeDownloader(error)
                result, _, _ = self.run_retry(downloader)
                self.assertEqual("payload", result)
                self.assertEqual(2, downloader.calls)


class RetryCommandTest(unittest.TestCase):
    """The command line wraps a program that gives no structured error, such as `west sdk install`."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.counter = self.root / "calls"

    def fake_downloader(self, failures, message, final="done"):
        """A program that prints `message` to stderr and exits 1 `failures` times, then succeeds."""
        program = self.root / "fake-download.py"
        program.write_text(textwrap.dedent(f"""
            import pathlib, sys
            counter = pathlib.Path({str(self.counter)!r})
            calls = int(counter.read_text()) + 1 if counter.exists() else 1
            counter.write_text(str(calls))
            if calls <= {failures}:
                print({message!r}, file=sys.stderr)
                sys.exit(1)
            print({final!r})
        """))
        return [sys.executable, str(program)]

    def run_wrapped(self, command, *options):
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--base-delay", "0", *options, "--", *command],
            capture_output=True, text=True, timeout=60)

    def calls(self):
        return int(self.counter.read_text())

    def test_the_command_is_repeated_until_it_succeeds(self):
        message = "Exception: Failed to download https://example.invalid/sha256.sum: 504"
        result = self.run_wrapped(self.fake_downloader(2, message))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(3, self.calls())
        self.assertIn("done", result.stdout)
        self.assertIn("attempt 1/4 failed", result.stderr)

    def test_a_toolchain_download_that_wget_reports_without_a_status_is_repeated(self):
        # The SDK's setup.sh runs `wget -q`, which hides the HTTP status; this line is all there is.
        message = "ERROR: GNU toolchain download failed"
        result = self.run_wrapped(self.fake_downloader(1, message))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(2, self.calls())

    def test_a_command_that_always_fails_with_a_5xx_stops_at_the_bound(self):
        message = "Exception: Failed to download https://example.invalid/sdk.tar.xz: 504"
        result = self.run_wrapped(self.fake_downloader(99, message))
        self.assertEqual(1, result.returncode)
        self.assertEqual(4, self.calls())
        self.assertIn("giving up after 4 attempts", result.stderr)

    def test_the_bound_can_be_lowered(self):
        message = "Exception: Failed to download https://example.invalid/sdk.tar.xz: 502"
        result = self.run_wrapped(self.fake_downloader(99, message), "--attempts", "2")
        self.assertEqual(1, result.returncode)
        self.assertEqual(2, self.calls())

    def test_a_checksum_mismatch_is_not_retried(self):
        result = self.run_wrapped(self.fake_downloader(99, "Exception: sha256 mismatched: aa:bb"))
        self.assertEqual(1, result.returncode)
        self.assertEqual(1, self.calls())
        self.assertIn("not a transient failure", result.stderr)

    def test_a_mismatch_wins_over_a_5xx_line_in_the_same_output(self):
        message = "Failed to download https://example.invalid/a: 504\nsha256 mismatched: aa:bb"
        result = self.run_wrapped(self.fake_downloader(99, message))
        self.assertEqual(1, result.returncode)
        self.assertEqual(1, self.calls())

    def test_a_4xx_is_not_retried(self):
        message = "Exception: Failed to download https://example.invalid/sdk.tar.xz: 404"
        result = self.run_wrapped(self.fake_downloader(99, message))
        self.assertEqual(1, result.returncode)
        self.assertEqual(1, self.calls())

    def test_an_unrelated_failure_keeps_its_exit_code_and_is_not_retried(self):
        program = self.root / "fail.py"
        program.write_text("import sys; print('unrelated error', file=sys.stderr); sys.exit(7)")
        result = self.run_wrapped([sys.executable, str(program)])
        self.assertEqual(7, result.returncode)
        self.assertIn("unrelated error", result.stderr)

    def test_a_missing_program_fails_at_once(self):
        result = self.run_wrapped([str(self.root / "no-such-program")])
        self.assertNotEqual(0, result.returncode)
        self.assertIn("no-such-program", result.stderr)

    def test_a_command_is_required(self):
        result = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(2, result.returncode)


class WiringTest(unittest.TestCase):
    """The scripts that CI runs must use the helper for the downloads that failed on 8 November."""

    def test_the_sdk_install_goes_through_the_helper(self):
        script = (REPO / "scripts/bootstrap-zephyr.sh").read_text()
        self.assertRegex(script, r'retry_transient\.py" -- \\\n\s+west sdk install')

    def test_the_lychee_download_is_retried_and_a_mismatch_is_still_fatal(self):
        import check_docs as docs
        policy = docs.load_policy()
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.dict(os.environ, {"CRUX_CACHE_DIR": str(Path(folder) / "cache")}), \
                mock.patch.object(retry.time, "sleep"):
            policy = {"tool": dict(policy["tool"])}
            policy["tool"]["linux_x86_64_sha256"] = "0" * 64
            policy["tool"]["macos_arm64_sha256"] = "0" * 64
            responses = [http_error(504), http_error(504), io.BytesIO(b"invalid archive")]
            with mock.patch.object(docs.urllib.request, "urlopen", side_effect=responses) as download:
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    docs.install_tool(policy, root=Path(folder))
            self.assertEqual(3, download.call_count)


if __name__ == "__main__":
    unittest.main()
