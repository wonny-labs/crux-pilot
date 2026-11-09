#!/usr/bin/env python3
"""Judge the UART capture of a bench run: stack headroom, heap, and the listener certificate (row C2).

The bench image prints a report on the UART at boot, after the traffic of a client has stopped, and once when it has
been idle (report/bench_report_format.h has the grammar, docs/rt1060-management-image-feasibility.md the table). This
script reads a capture of that UART and fails the run when

  * the HTTP server thread's lowest unused stack, over every report, is below a margin (default 25 percent of its
    stack, which is 2,048 B of 8,192 B: see the document for why that number),
  * the Wolf heap's peak is above a share of the arena (default 80 percent),
  * a network pool of row C6 (packets or data buffers, each way) peaked above a share of its size (default 80
    percent), or a pool's size differs from what the build declared,
  * a report is torn, short a line, out of sequence, over the line limit, or its rows do not add up,
  * the sizes differ from what the build declared (--expect-http-stack-bytes, or --manifest bench-manifest.json),
  * there is no boot report or no report after a client, or no BENCH-CERT line, or the fingerprint on the UART is
    not the one the listener serves (--cert-fingerprint, --cert-pem, or --connect HOST:PORT, which asks the
    listener with `openssl s_client`).

Other threads are judged by the same margin and only warn, unless --strict or --fail-thread NAME says otherwise.
Exit status: 0 PASS, 1 FAIL, 2 the script could not do its job (a missing file, no openssl). It touches no
hardware: it reads a file, and with --connect it makes one TLS connection to the address it is given.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

LINE_LIMIT = 159  # BENCH_REPORT_LINE_MAX - 1 in report/bench_report_format.h
HTTP_THREAD = "http_server_tid"
NAME = r"[A-Za-z0-9_.\[\]@-]{1,23}"
FINGERPRINT = r"(?:[0-9A-F]{2}:){31}[0-9A-F]{2}"
MARKER = re.compile(r"(BENCH-(?:REPORT|STACK|HEAP|CERT|POOL|NET)\b.*)$")
BEGIN = re.compile(r"^BENCH-REPORT v=1 begin seq=(\d+) reason=(boot|quiet|idle) uptime_ms=(\d+) bursts=(\d+)$")
END = re.compile(r"^BENCH-REPORT v=1 end seq=(\d+) lines=(\d+) missed=(\d+)$")
STACK = re.compile(rf"^BENCH-STACK seq=(\d+) thread=({NAME}) size=(\d+) used=(\d+) unused=(\d+) state=(live|ended)$")
HEAP = re.compile(r"^BENCH-HEAP seq=(\d+) arena=(\d+) in_use=(\d+) peak=(\d+) free=(\d+)$")
POOL = re.compile(r"^BENCH-POOL seq=(\d+) pool=(rx_pkt|tx_pkt|rx_buf|tx_buf) size=(\d+) used=(\d+) peak=(\d+)$")
NET_COUNTERS = ("ip_drop", "tcp_drop", "tcp_seg_drop", "tcp_rexmit", "tcp_conndrop")
NET = re.compile(r"^BENCH-NET seq=(\d+) " + " ".join(rf"{name}=(\d+)" for name in NET_COUNTERS) + "$")
# The pools of row C6 and the Kconfig symbols that size them, which the manifest records.
POOL_SYMBOLS = {"rx_pkt": "CONFIG_NET_PKT_RX_COUNT", "tx_pkt": "CONFIG_NET_PKT_TX_COUNT",
                "rx_buf": "CONFIG_NET_BUF_RX_COUNT", "tx_buf": "CONFIG_NET_BUF_TX_COUNT"}
CERT = re.compile(rf"^BENCH-CERT v=1 sha256=({FINGERPRINT}) source=(generated|stored)$")
OPENSSL_FINGERPRINT = re.compile(r"[Ff]ingerprint=((?:[0-9A-Fa-f]{2}:){31}[0-9A-Fa-f]{2})\s*$", re.MULTILINE)
NEXT_SIZE = 2


class CheckError(Exception):
    """The script cannot judge: bad input that is not the board's fault, or a tool that is missing."""


def fingerprint_from_openssl_output(text):
    """The SHA-256 fingerprint of `openssl x509 -noout -fingerprint -sha256`, in the module's upper case form."""
    found = OPENSSL_FINGERPRINT.search(text)
    if not found or not re.match(r"^sha256\b", text.strip(), re.IGNORECASE):
        raise CheckError(f"openssl did not print a SHA-256 fingerprint: {text.strip()[:120]!r}")
    return found.group(1).upper()


def _openssl(arguments, data=None):
    try:
        result = subprocess.run(["openssl", *arguments], input=data, capture_output=True, timeout=30, check=False)
    except FileNotFoundError as error:
        raise CheckError("openssl is not installed") from error
    except subprocess.TimeoutExpired as error:
        raise CheckError(f"openssl {arguments[0]} timed out") from error
    return result


def fingerprint_from_pem(path):
    result = _openssl(["x509", "-in", str(path), "-noout", "-fingerprint", "-sha256"])
    if result.returncode != 0:
        raise CheckError(f"openssl could not read {path}: {result.stderr.decode(errors='replace').strip()}")
    return fingerprint_from_openssl_output(result.stdout.decode(errors="replace"))


def fingerprint_from_connect(target):
    """What the listener serves: the certificate that `openssl s_client` receives, hashed by openssl."""
    session = _openssl(["s_client", "-connect", target, "-servername", "crux-bench"], data=b"")
    certificate = session.stdout.decode(errors="replace")
    if "BEGIN CERTIFICATE" not in certificate:
        raise CheckError(f"no certificate from {target}: {session.stderr.decode(errors='replace').strip()[:200]}")
    result = _openssl(["x509", "-noout", "-fingerprint", "-sha256"], data=session.stdout)
    if result.returncode != 0:
        raise CheckError("openssl could not read the certificate that the listener sent")
    return fingerprint_from_openssl_output(result.stdout.decode(errors="replace"))


def normalise_fingerprint(text):
    text = re.sub(r"^\s*(?:sha-?256\s*)?(?:fingerprint\s*=\s*)?", "", text.strip(), flags=re.IGNORECASE).upper()
    if not re.fullmatch(FINGERPRINT, text):
        raise CheckError(f"not a SHA-256 fingerprint of 32 pairs joined by colons: {text[:100]!r}")
    return text


class Capture:
    """The events of a capture, and what is wrong with their shape."""

    def __init__(self, text):
        self.failures = []
        self.reports = []
        self.certificates = []
        self._parse(text)

    def fail(self, message):
        self.failures.append(message)

    def _parse(self, text):
        current = None
        expected_seq = None
        for number, raw in enumerate(text.splitlines(), 1):
            found = MARKER.search(raw.rstrip("\r\n"))
            if not found:
                continue
            line = found.group(1).rstrip()
            where = f"line {number}"
            if len(line) > LINE_LIMIT:
                self.fail(f"{where}: {len(line)} characters, over the limit of {LINE_LIMIT}")
                continue
            if line.startswith("BENCH-CERT"):
                match = CERT.match(line)
                if match:
                    self.certificates.append(match.group(1))
                else:
                    self.fail(f"{where}: malformed BENCH-CERT line: {line[:80]!r}")
                continue
            begin = BEGIN.match(line)
            if begin:
                if current is not None:
                    self.fail(f"{where}: report seq={current['seq']} has no end line (torn report)")
                seq = int(begin.group(1))
                reason = begin.group(2)
                if expected_seq is not None and seq != expected_seq and not (reason == "boot" and seq == 1):
                    self.fail(f"{where}: seq={seq} follows a report that makes {expected_seq} next (a lost report)")
                current = {"seq": seq, "reason": reason, "uptime_ms": int(begin.group(3)), "bursts": int(begin.group(4)),
                           "stack": [], "heap": [], "pool": [], "net": [], "line": number}
                continue
            if current is None:
                self.fail(f"{where}: a report line outside a report: {line[:60]!r}")
                continue
            stack, heap, end = STACK.match(line), HEAP.match(line), END.match(line)
            pool, net = POOL.match(line), NET.match(line)
            if pool:
                seq, name, size, used, peak = int(pool.group(1)), pool.group(2), *map(int, pool.group(3, 4, 5))
                if seq != current["seq"]:
                    self.fail(f"{where}: seq={seq} inside report seq={current['seq']}")
                if used > size or peak > size:
                    self.fail(f"{where}: {name}: used {used} and peak {peak} must not be over size {size}")
                if any(row["name"] == name for row in current["pool"]):
                    self.fail(f"{where}: report seq={current['seq']} has a second row for pool {name}")
                current["pool"].append({"name": name, "size": size, "used": used, "peak": peak})
            elif net:
                values = list(map(int, net.groups()))
                if values[0] != current["seq"]:
                    self.fail(f"{where}: seq={values[0]} inside report seq={current['seq']}")
                current["net"].append(dict(zip(NET_COUNTERS, values[1:])))
            elif stack:
                seq, name, size, used, unused = int(stack.group(1)), stack.group(2), *map(int, stack.group(3, 4, 5))
                if seq != current["seq"]:
                    self.fail(f"{where}: seq={seq} inside report seq={current['seq']}")
                if used + unused != size:
                    self.fail(f"{where}: {name}: used {used} + unused {unused} is not size {size}")
                current["stack"].append({"name": name, "size": size, "used": used, "unused": unused,
                                         "state": stack.group(6)})
            elif heap:
                values = list(map(int, heap.groups()))
                if values[0] != current["seq"]:
                    self.fail(f"{where}: seq={values[0]} inside report seq={current['seq']}")
                current["heap"].append({"arena": values[1], "in_use": values[2], "peak": values[3], "free": values[4]})
            elif end:
                seq, lines, missed = map(int, end.groups())
                if seq != current["seq"]:
                    self.fail(f"{where}: end seq={seq} closes report seq={current['seq']}")
                counted = len(current["stack"]) + len(current["heap"]) + len(current["pool"]) + len(current["net"])
                if lines != counted:
                    self.fail(f"report seq={current['seq']}: end says lines={lines}, but {counted} "
                              "BENCH-STACK, BENCH-HEAP, BENCH-POOL and BENCH-NET lines arrived "
                              "(a line was lost or garbled)")
                if current["pool"] and {row["name"] for row in current["pool"]} != set(POOL_SYMBOLS):
                    missing = sorted(set(POOL_SYMBOLS) - {row["name"] for row in current["pool"]})
                    self.fail(f"report seq={current['seq']}: pools {missing} have no BENCH-POOL row")
                if len(current["net"]) > 1:
                    self.fail(f"report seq={current['seq']}: {len(current['net'])} BENCH-NET lines, at most 1")
                if len(current["heap"]) != 1:
                    self.fail(f"report seq={current['seq']}: {len(current['heap'])} heap lines, needs 1")
                if missed != 0:
                    self.fail(f"report seq={current['seq']}: {missed} threads missed (the table was full)")
                current["missed"] = missed
                self.reports.append(current)
                expected_seq = current["seq"] + 1
                current = None
            else:
                self.fail(f"{where}: malformed report line: {line[:80]!r}")
        if current is not None:
            self.fail(f"report seq={current['seq']} has no end line (torn report, or the capture stopped inside it)")


def judge_pools(reports, options, expected_pools, failures, warnings):
    """Row C6: the pools' high-water marks against their sizes, the sizes against the build, and the counters."""
    rows = [(report["seq"], row) for report in reports for row in report["pool"]]
    if not rows:
        if options.require_pools or expected_pools:
            failures.append("no BENCH-POOL line: the build declares network pools (row C6) and the run did not report "
                            "them" if expected_pools else "no BENCH-POOL line (--require-pools)")
        return None, None
    pools = {}
    for _, row in rows:
        entry = pools.setdefault(row["name"], {"size": row["size"], "peak_max": row["peak"], "sizes": {row["size"]}})
        entry["peak_max"] = max(entry["peak_max"], row["peak"])
        entry["sizes"].add(row["size"])
    for name, entry in sorted(pools.items()):
        if len(entry["sizes"]) > 1:
            failures.append(f"pool {name}: rows give different sizes {sorted(entry['sizes'])}")
        entry["size"] = max(entry.pop("sizes"))
        if name in expected_pools and entry["size"] != expected_pools[name]:
            failures.append(f"pool {name}: size {entry['size']}, but the build declares {expected_pools[name]} "
                            "(a stale image, or the wrong manifest)")
        entry["peak_percent"] = 100.0 * entry["peak_max"] / entry["size"] if entry["size"] else 0.0
        entry["limit_percent"] = options.pool_peak_limit_percent
        if entry["peak_max"] * 100 > entry["size"] * options.pool_peak_limit_percent:
            failures.append(f"pool {name}: peak {entry['peak_max']} of {entry['size']} ({entry['peak_percent']:.1f} "
                            f"percent) is over {options.pool_peak_limit_percent} percent")
    # The counters count from boot, so the last report holds the totals.
    counters = [report["net"][-1] for report in reports if report["net"]]
    net = counters[-1] if counters else None
    if net is not None:
        for name in NET_COUNTERS:
            if net[name] != 0:
                (failures if options.strict else warnings).append(
                    f"{name}: {net[name]} at the last report (a short pool, or a bad link)")
    return pools, net


def judge(capture, options, manifest):
    """Returns (failures, warnings, summary) for a parsed capture."""
    failures = list(capture.failures)
    warnings = []
    reports = capture.reports
    expected_http = options.expect_http_stack_bytes
    expected_arena = None
    expected_pools = {}
    if manifest is not None:
        settings = manifest.get("settings", {})
        for name, symbol in POOL_SYMBOLS.items():
            if symbol in settings:
                expected_pools[name] = int(settings[symbol])
        if expected_http is None and "CONFIG_HTTP_SERVER_STACK_SIZE" in settings:
            expected_http = int(settings["CONFIG_HTTP_SERVER_STACK_SIZE"])
        if "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE" in settings:
            expected_arena = int(settings["CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE"])

    if not reports:
        failures.append("no complete report in the capture")
    if reports and not any(report["reason"] == "boot" for report in reports):
        failures.append("no boot report: the capture does not hold the idle baseline")
    clients = sum(1 for report in reports if report["reason"] == "quiet")
    if clients < options.min_client_reports:
        failures.append(f"{clients} reports after a client, {options.min_client_reports} required (reason=quiet)")

    threads = {}
    for report in reports:
        names = [row["name"] for row in report["stack"] if row["state"] == "live"]
        if options.http_thread not in names and not any(
                row["name"] == options.http_thread for row in report["stack"]):
            failures.append(f"report seq={report['seq']}: no row for {options.http_thread}")
        for row in report["stack"]:
            entry = threads.setdefault((row["name"], row["size"]), {"size": row["size"], "unused_min": row["unused"],
                                                                    "used_max": row["used"], "rows": 0})
            entry["unused_min"] = min(entry["unused_min"], row["unused"])
            entry["used_max"] = max(entry["used_max"], row["used"])
            entry["rows"] += 1
    # Two threads can share a name (the kernel cuts it, or a driver names two alike) with stacks of different sizes.
    # They are judged apart, as name/size, and the HTTP thread's name must be unambiguous.
    sizes_of = {}
    for name, size in threads:
        sizes_of.setdefault(name, set()).add(size)
    judged = {}
    for (name, size), entry in sorted(threads.items()):
        key = name if len(sizes_of[name]) == 1 else f"{name}/{size}"
        if len(sizes_of[name]) > 1:
            warnings.append(f"{name}: rows give different sizes {sorted(sizes_of[name])}, judged apart as {key}")
        entry["unused_percent_min"] = 100.0 * entry["unused_min"] / entry["size"] if entry["size"] else 0.0
        judged[key] = entry
    threads = judged
    if len(sizes_of.get(options.http_thread, ())) > 1:
        failures.append(f"{options.http_thread}: rows give different sizes {sorted(sizes_of[options.http_thread])}")

    http = threads.get(options.http_thread)
    if http is not None:
        if expected_http is not None and http["size"] != expected_http:
            failures.append(f"{options.http_thread}: size {http['size']} B, but the build declares {expected_http} B "
                            "(a stale image, or the wrong manifest)")
        needed = -(-http["size"] * options.http_margin_percent // 100)
        http["margin_percent"] = options.http_margin_percent
        http["margin_bytes"] = needed
        if http["unused_min"] < needed:
            failures.append(
                f"{options.http_thread}: lowest unused stack {http['unused_min']} B of {http['size']} B "
                f"({http['unused_percent_min']:.1f} percent) is under the margin of {options.http_margin_percent} percent "
                f"({needed} B). Next size {http['size'] * NEXT_SIZE} B, then measure again.")
    for name, entry in sorted(threads.items()):
        if name == options.http_thread:
            continue
        needed = -(-entry["size"] * options.other_margin_percent // 100)
        if entry["unused_min"] < needed:
            message = (f"{name}: lowest unused stack {entry['unused_min']} B of {entry['size']} B "
                       f"({entry['unused_percent_min']:.1f} percent) is under {options.other_margin_percent} percent")
            (failures if options.strict or name in options.fail_thread else warnings).append(message)

    heaps = [row for report in reports for row in report["heap"]]
    heap = None
    if heaps:
        arenas = {row["arena"] for row in heaps}
        if len(arenas) > 1:
            failures.append(f"heap rows give different arenas {sorted(arenas)}")
        arena = max(arenas)
        if expected_arena is not None and arena != expected_arena:
            failures.append(f"heap arena {arena} B, but the build declares {expected_arena} B")
        peak = max(row["peak"] for row in heaps)
        heap = {"arena": arena, "peak_max": peak, "in_use_max": max(row["in_use"] for row in heaps),
                "peak_percent": 100.0 * peak / arena if arena else 0.0, "limit_percent": options.heap_peak_limit_percent}
        if peak * 100 > arena * options.heap_peak_limit_percent:
            failures.append(f"heap peak {peak} B of {arena} B ({heap['peak_percent']:.1f} percent) is over "
                            f"{options.heap_peak_limit_percent} percent")

    pools, net = judge_pools(reports, options, expected_pools, failures, warnings)

    certificate = capture.certificates[-1] if capture.certificates else None
    if certificate is None and not options.skip_cert:
        failures.append("no BENCH-CERT line: the certificate fingerprint of D4 is not on the UART")
    reference = None
    if options.cert_fingerprint:
        reference = normalise_fingerprint(options.cert_fingerprint)
    elif options.cert_pem:
        reference = fingerprint_from_pem(options.cert_pem)
    elif options.connect:
        reference = fingerprint_from_connect(options.connect)
    if reference is not None and certificate is not None and certificate != reference:
        failures.append(f"the UART's certificate fingerprint {certificate} differs from the one compared {reference}")
    if len(capture.certificates) > 1:
        warnings.append(f"{len(capture.certificates)} BENCH-CERT lines (the board booted more than once): "
                        "the last one is the one compared")

    summary = {"verdict": "FAIL" if failures else "PASS", "reports": len(reports),
               "reasons": {reason: sum(1 for r in reports if r["reason"] == reason) for reason in ("boot", "quiet", "idle")},
               "threads": threads, "heap": heap, "pools": pools, "net": net, "certificate": certificate,
               "compared_with": reference, "failures": failures, "warnings": warnings}
    return failures, warnings, summary


def print_human(summary):
    out = sys.stdout
    reasons = summary["reasons"]
    print(f"{summary['reports']} reports (boot {reasons['boot']}, quiet {reasons['quiet']}, idle {reasons['idle']})", file=out)
    print(f"{'thread':<24}{'size':>8}{'used max':>10}{'unused min':>12}{'unused %':>10}", file=out)
    for name, entry in sorted(summary["threads"].items()):
        print(f"{name:<24}{entry['size']:>8}{entry['used_max']:>10}{entry['unused_min']:>12}"
              f"{entry['unused_percent_min']:>9.1f}%", file=out)
    if summary["heap"]:
        heap = summary["heap"]
        print(f"heap: peak {heap['peak_max']} B of {heap['arena']} B ({heap['peak_percent']:.1f} percent, limit "
              f"{heap['limit_percent']}), in use at most {heap['in_use_max']} B", file=out)
    if summary["pools"]:
        print(f"{'pool':<24}{'size':>8}{'peak max':>10}{'peak %':>10}", file=out)
        for name, entry in sorted(summary["pools"].items()):
            print(f"{name:<24}{entry['size']:>8}{entry['peak_max']:>10}{entry['peak_percent']:>9.1f}%", file=out)
    if summary["net"]:
        print("net: " + " ".join(f"{name}={value}" for name, value in summary["net"].items()), file=out)
    print(f"certificate: {summary['certificate']}", file=out)
    for message in summary["warnings"]:
        print(f"WARN {message}", file=out)
    for message in summary["failures"]:
        print(f"FAIL {message}", file=out)
    print(summary["verdict"], file=out)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("capture", help="the UART capture, a text file")
    parser.add_argument("--manifest", type=Path, help="bench-manifest.json of the build, for the declared sizes")
    parser.add_argument("--expect-http-stack-bytes", type=int)
    parser.add_argument("--http-thread", default=HTTP_THREAD)
    parser.add_argument("--http-margin-percent", type=int, default=25,
                        help="the HTTP thread's lowest unused stack, as a share of its stack (default 25)")
    parser.add_argument("--other-margin-percent", type=int, default=25)
    parser.add_argument("--heap-peak-limit-percent", type=int, default=80)
    parser.add_argument("--pool-peak-limit-percent", type=int, default=80,
                        help="a network pool's peak, as a share of its size (default 80, row C6)")
    parser.add_argument("--require-pools", action="store_true",
                        help="fail a capture with no BENCH-POOL line (a manifest that declares the pools asks for them)")
    parser.add_argument("--min-client-reports", type=int, default=1,
                        help="reports with reason=quiet that the run must hold (default 1)")
    parser.add_argument("--strict", action="store_true",
                        help="a thread under the margin, and a non-zero BENCH-NET counter, fail and do not only warn")
    parser.add_argument("--fail-thread", action="append", default=[], metavar="NAME")
    parser.add_argument("--skip-cert", action="store_true", help="do not require a BENCH-CERT line")
    compare = parser.add_mutually_exclusive_group()
    compare.add_argument("--cert-fingerprint", help="the fingerprint to compare with, as openssl prints it")
    compare.add_argument("--cert-pem", type=Path, help="a PEM file, hashed with openssl")
    compare.add_argument("--connect", metavar="HOST:PORT", help="fetch the listener's certificate with openssl s_client")
    parser.add_argument("--json", action="store_true", help="print the summary as JSON only")
    return parser.parse_args(argv)


def main(argv=None):
    options = parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        text = Path(options.capture).read_bytes().decode("latin-1")
        manifest = json.loads(options.manifest.read_text()) if options.manifest else None
        capture = Capture(text)
        _, _, summary = judge(capture, options, manifest)
    except (OSError, ValueError, CheckError) as error:
        print(f"check_bench_uart: {error}", file=sys.stderr)
        return 2
    if options.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_human(summary)
    return 0 if summary["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
