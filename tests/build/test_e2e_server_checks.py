#!/usr/bin/env python3
"""Mutations of the real-server checks of Issue #220, on synthetic answers.

tests/native/management_services/e2e/server_checks.py runs these checks against a native_sim image. Each test here
feeds one a correct answer, which must pass, and answers that break one rule each: the wrong policy, a policy
without frame-ancestors, the other style's policy, the wrong cache class, a lost encoding, other bytes, a plain
listener, a deep-link fallback, a TLS 1.2 handshake accepted, and an API answer without the core's headers.
"""
import gzip
import hashlib
import importlib.util
import pathlib
import unittest

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "server_checks", REPOSITORY / "tests/native/management_services/e2e/server_checks.py")
checks = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checks)

STRICT = checks.ASSET_POLICY["strict"]
INLINE = checks.ASSET_POLICY["inline"]


def asset(path="/assets/app.2WOWJ6PS.js", cache="immutable", encoding="gzip", body=b"served bytes"):
    return {"path": path, "cache": cache, "encoding": encoding,
            "served_sha256": hashlib.sha256(body).hexdigest()}


def headers(policy=STRICT, cache="max-age=31536000, immutable", encoding="gzip"):
    found = {"content-security-policy": policy, "x-content-type-options": "nosniff",
             "referrer-policy": "no-referrer", "cache-control": cache}
    if encoding:
        found["content-encoding"] = encoding
    return found


class AssetChecks(unittest.TestCase):
    def test_a_correct_answer_passes_under_each_style(self):
        self.assertEqual(checks.asset_errors(asset(), 200, headers(), b"served bytes", "strict"), [])
        self.assertEqual(checks.asset_errors(asset(), 200, headers(INLINE), b"served bytes", "inline"), [])

    def test_the_policies_are_the_documented_text(self):
        self.assertEqual(STRICT, "default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; "
                                 "form-action 'self'; frame-ancestors 'none'")
        self.assertEqual(INLINE, STRICT.replace("style-src 'self'", "style-src 'self' 'unsafe-inline'"))

    def test_the_other_styles_policy_fails(self):
        self.assertTrue(checks.asset_errors(asset(), 200, headers(INLINE), b"served bytes", "strict"))
        self.assertTrue(checks.asset_errors(asset(), 200, headers(STRICT), b"served bytes", "inline"))

    def test_a_wrong_policy_fails(self):
        for policy in (None, "default-src 'self'", STRICT.replace("; frame-ancestors 'none'", ""),
                       STRICT.replace("script-src 'self'", "script-src 'self' 'unsafe-inline'"),
                       STRICT + "; img-src data:"):
            with self.subTest(policy=policy):
                self.assertTrue(checks.asset_errors(asset(), 200, headers(policy), b"served bytes", "strict"))

    def test_a_missing_frame_ancestors_is_named(self):
        errors = checks.asset_errors(asset(), 200, headers(STRICT.replace("; frame-ancestors 'none'", "")),
                                     b"served bytes", "strict")
        self.assertTrue(any("frame-ancestors" in error for error in errors))

    def test_a_wrong_cache_header_fails(self):
        self.assertTrue(checks.asset_errors(asset(), 200, headers(cache="no-cache"), b"served bytes", "strict"))
        unhashed = asset("/favicon.svg", cache="revalidate")
        self.assertEqual(checks.asset_errors(unhashed, 200, headers(cache="no-cache"), b"served bytes", "strict"), [])
        self.assertTrue(checks.asset_errors(unhashed, 200, headers(), b"served bytes", "strict"))
        self.assertTrue(checks.asset_errors(unhashed, 200, headers(cache="max-age=60"), b"served bytes", "strict"))

    def test_nosniff_and_referrer_are_required(self):
        for name in ("x-content-type-options", "referrer-policy"):
            found = headers()
            del found[name]
            with self.subTest(header=name):
                self.assertTrue(checks.asset_errors(asset(), 200, found, b"served bytes", "strict"))

    def test_a_lost_encoding_or_other_bytes_fail(self):
        self.assertTrue(checks.asset_errors(asset(), 200, headers(encoding=None), b"served bytes", "strict"))
        self.assertTrue(checks.asset_errors(asset(), 200, headers(), b"other bytes", "strict"))
        self.assertTrue(checks.asset_errors(asset(), 404, headers(), b"served bytes", "strict"))


class RouteChecks(unittest.TestCase):
    INDEX = b"<!doctype html><script src=/assets/app.js></script>"

    def test_a_plain_404_passes(self):
        self.assertEqual(checks.fallback_errors("/config", 404, b"Not Found", self.INDEX), [])

    def test_a_deep_link_fallback_fails(self):
        self.assertTrue(checks.fallback_errors("/config", 200, self.INDEX, self.INDEX))
        self.assertTrue(checks.fallback_errors("/config", 404, self.INDEX, self.INDEX))
        self.assertTrue(checks.fallback_errors("/config", 200, b"", self.INDEX))

    def test_the_fragment_routes_cover_each_screen(self):
        for path in ("/config", "/home", "/network", "/signin"):
            self.assertIn(path, checks.FRAGMENT_ROUTES)


class ListenerChecks(unittest.TestCase):
    def test_tls_1_3_only_passes(self):
        self.assertEqual(checks.tls_errors({"established": False},
                                           {"established": True, "protocol": "TLSv1.3", "verified": True}), [])

    def test_tls_1_2_accepted_fails(self):
        self.assertTrue(checks.tls_errors({"established": True, "protocol": "TLSv1.2"},
                                          {"established": True, "protocol": "TLSv1.3", "verified": True}))

    def test_no_tls_1_3_or_no_verification_fails(self):
        self.assertTrue(checks.tls_errors({"established": False}, {"established": False}))
        self.assertTrue(checks.tls_errors({"established": False},
                                          {"established": True, "protocol": "TLSv1.3", "verified": False}))

    def test_a_plain_listener_fails(self):
        self.assertEqual(checks.plain_errors(b""), [])
        self.assertEqual(checks.plain_errors(b"\x15\x03\x03\x00\x02\x02\x46"), [])
        self.assertTrue(checks.plain_errors(b"HTTP/1.1 200 OK\r\n"))

    def test_the_api_keeps_the_core_headers(self):
        good = {"content-security-policy": checks.CORE_POLICY, "cache-control": "no-store",
                "x-content-type-options": "nosniff"}
        self.assertEqual(checks.core_errors(401, good), [])
        for name, value in (("content-security-policy", STRICT), ("cache-control", "no-cache"),
                            ("x-content-type-options", None)):
            changed = dict(good, **{name: value})
            with self.subTest(header=name):
                self.assertTrue(checks.core_errors(401, changed))


class Parsing(unittest.TestCase):
    def test_a_chunked_gzip_answer_is_read_whole(self):
        body = gzip.compress(b"hello")
        raw = (b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nSet-Cookie: a=1\r\n\r\n" +
               f"{len(body):x}\r\n".encode() + body + b"\r\n0\r\n\r\n")
        reply = checks.parse(raw)
        self.assertEqual(reply.status, 200)
        self.assertEqual(reply.body, body)
        self.assertEqual(reply.headers["set-cookie"], ["a=1"])
        self.assertIsNone(checks.parse(raw[:-7]))


if __name__ == "__main__":
    unittest.main()
