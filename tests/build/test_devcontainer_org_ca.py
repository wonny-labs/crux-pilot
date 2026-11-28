"""Checks for trusting an organisation's root CA in the Dev Container image.

A developer drops the CA, as a PEM or DER .crt file, into .devcontainer/certs/ and rebuilds the
container. These tests prove that the Dockerfile installs it with
.devcontainer/install-org-ca.sh after the package install and before every download, that
Git keeps certificates out of the repository, that the image points run-time tools at the
system bundle, and that the install script accepts PEM certificates in .crt, .pem and .cer
files, converts exactly one DER certificate with openssl, and refuses keys, PKCS#7, PKCS#12 and other content. They run the script with stubbed openssl and update-ca-certificates
commands, and, where openssl is installed, with a throw-away self-signed certificate. They
use no network and prove nothing about a real TLS-inspecting proxy.
"""
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
DEVCONTAINER = REPO / ".devcontainer"
CERTS = DEVCONTAINER / "certs"
INSTALL = DEVCONTAINER / "install-org-ca.sh"
DOCKERFILE = DEVCONTAINER / "Dockerfile"
BUNDLE = "/etc/ssl/certs/ca-certificates.crt"
PEM = ("-----BEGIN CERTIFICATE-----\nMIIBfakebase64data\n-----END CERTIFICATE-----\n")


def run_steps(text):
    """Return the Dockerfile instructions in order, with continuation lines joined."""
    steps, current = [], ""
    for line in text.splitlines():
        if not current and (not line.strip() or line.lstrip().startswith("#")):
            continue
        current += line.rstrip("\\").strip() + " "
        if not line.rstrip().endswith("\\"):
            steps.append(current.strip())
            current = ""
    return steps


class DockerfileOrderTest(unittest.TestCase):
    """The CA is installed after ca-certificates and before every network download."""

    def setUp(self):
        self.steps = run_steps(DOCKERFILE.read_text())

    def index(self, predicate, what):
        for number, step in enumerate(self.steps):
            if predicate(step):
                return number
        self.fail(f"no Dockerfile step {what}")

    def test_certs_are_bind_mounted_and_never_copied_into_a_layer(self):
        # A BuildKit bind mount exposes the folder to one RUN step only; no file from it is stored in a layer.
        install = self.index(lambda s: s.startswith("RUN ") and "install-org-ca.sh" in s, "runs the script")
        self.assertIn("--mount=type=bind,source=.devcontainer/certs,target=/tmp/crux-certs", self.steps[install])
        self.assertIn("/tmp/crux-certs", self.steps[install].split("install-org-ca.sh", 1)[1])
        for step in self.steps:
            if step.startswith(("COPY ", "ADD ")):
                self.assertNotIn(".devcontainer/certs", step)

    def test_install_follows_ca_certificates_and_precedes_every_download(self):
        packages = self.index(lambda s: s.startswith("RUN ") and "apt-get install" in s and "ca-certificates" in s,
                              "installs ca-certificates")
        install = self.index(lambda s: s.startswith("RUN ") and "install-org-ca.sh" in s, "runs the script")
        self.assertLess(packages, install)
        self.assertNotIn("pip install", self.steps[packages], "pip must run after the CA is trusted")
        # The package install itself comes first: it brings ca-certificates and openssl.
        downloads = [n for n, s in enumerate(self.steps)
                     if n != packages and s.startswith("RUN ") and re.search(r"pip install|install-jlink\.sh|curl |wget |git clone", s)]
        self.assertTrue(downloads)
        for number in downloads:
            self.assertLess(install, number, self.steps[number])

    def test_run_time_tools_use_the_system_bundle(self):
        # The same six variables as the organisation's other Dev Containers, set before any download.
        env = self.index(lambda s: s.startswith("ENV ") and "SSL_CERT_FILE" in s, "sets SSL_CERT_FILE")
        pip = self.index(lambda s: s.startswith("RUN ") and "pip install" in s, "installs Python packages")
        self.assertLess(env, pip)
        for name in ("REQUESTS_CA_BUNDLE", "CURL_CA_BUNDLE", "SSL_CERT_FILE", "GIT_SSL_CAINFO", "PIP_CERT",
                     "NODE_EXTRA_CA_CERTS"):
            self.assertIn(f"{name}={BUNDLE}", self.steps[env], name)

    def test_no_step_turns_certificate_checks_off(self):
        lowered = DOCKERFILE.read_text().lower() + INSTALL.read_text().lower()
        for weakening in ("--no-check-certificate", "curl -k", "--insecure", "--trusted-host",
                          "git_ssl_no_verify", "sslverify=false", "pythonhttpsverify=0"):
            self.assertNotIn(weakening, lowered)


class CertsFolderTest(unittest.TestCase):
    """Git carries only .gitkeep and the README there, never a certificate.

    Each developer adds their own CA locally; no certificate is committed anywhere.
    """

    KEPT = [".devcontainer/certs/.gitkeep", ".devcontainer/certs/README.md"]

    def test_folder_tracks_nothing_besides_gitkeep_and_the_readme(self):
        tracked = subprocess.run(["git", "-C", str(REPO), "ls-files", "--", ".devcontainer/certs"],
                                 check=True, capture_output=True, text=True).stdout.split()
        self.assertEqual(sorted(tracked), self.KEPT)

    def test_no_certificate_is_tracked_anywhere(self):
        tracked = subprocess.run(["git", "-C", str(REPO), "ls-files", "--", ".devcontainer"],
                                 check=True, capture_output=True, text=True).stdout.split()
        self.assertEqual([path for path in tracked if re.search(r"\.(crt|cer|pem|der)$", path)], [])

    def test_gitignore_keeps_certificates_out(self):
        lines = [line.strip() for line in (REPO / ".gitignore").read_text().splitlines()]
        start = lines.index(".devcontainer/certs/*")
        self.assertEqual(lines[start:start + 3], [".devcontainer/certs/*", "!.devcontainer/certs/.gitkeep",
                                                  "!.devcontainer/certs/README.md"])
        self.assertFalse((CERTS / ".gitignore").exists())
        for name in ("org-root.crt", "org-root.cer", "org-root.pem", "org-root.der", "notes.txt", ".gitignore"):
            result = subprocess.run(["git", "-C", str(REPO), "check-ignore", "-q", "--no-index",
                                     f".devcontainer/certs/{name}"])
            self.assertEqual(result.returncode, 0, name)
        for name in ("README.md", ".gitkeep"):
            result = subprocess.run(["git", "-C", str(REPO), "check-ignore", "-q", "--no-index",
                                     f".devcontainer/certs/{name}"])
            self.assertEqual(result.returncode, 1, name)

    def test_readme_explains_export_issuer_and_conversion(self):
        text = (CERTS / "README.md").read_text()
        for needle in ("certmgr.msc", "Trusted Root Certification Authorities", "Base-64 encoded X.509",
                       "openssl s_client -connect", "openssl x509 -noout -issuer",
                       "openssl x509 -inform der -in", "curl: (60) SSL certificate problem",
                       "your organisation's root CA"):
            self.assertIn(needle, text)

    def test_guides_point_to_the_readme(self):
        for page in ("docs/getting-started.md", "docs/board-jlink-usbipd.md"):
            text = (REPO / page).read_text()
            self.assertIn("curl: (60) SSL certificate problem", text, page)
            self.assertIn(".devcontainer/certs/README.md", text, page)


class InstallScriptTest(unittest.TestCase):
    """install-org-ca.sh with stubbed openssl and update-ca-certificates."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.src = self.tmp / "certs"
        self.dest = self.tmp / "dest"
        self.bin = self.tmp / "bin"
        self.log = self.tmp / "calls.log"
        for folder in (self.src, self.dest, self.bin):
            folder.mkdir()
        (self.src / "README.md").write_text("readme\n")
        (self.src / ".gitignore").write_text("*\n")
        (self.src / ".gitkeep").write_text("")
        # openssl stub: a certificate whose body says BROKEN does not parse.
        # "-inform der" converts a file whose text starts with DER (a stand-in for DER bytes)
        # and fails on anything else. Like the real openssl, it reads only the first object
        # (here: the first line) and ignores the rest; "-outform der" gives that object back.
        self.stub("openssl", 'echo "openssl $*" >>"$CALLS"\n'
                  'case " $* " in *" -inform der "*)\n'
                  '  while [ $# -gt 0 ]; do case $1 in -in) i=$2;; -out) o=$2;; esac; shift; done\n'
                  '  grep -q "^DER" "$i" || { echo "unable to load certificate" >&2; exit 1; }\n'
                  '  printf "%s\\n" "-----BEGIN CERTIFICATE-----" MIIBconverted "-----END CERTIFICATE-----" >"$o"\n'
                  '  head -n 1 "$i" >"$o.der"\n'
                  '  exit 0;;\n'
                  '*" -outform der "*)\n'
                  '  while [ $# -gt 0 ]; do case $1 in -in) i=$2;; -out) o=$2;; esac; shift; done\n'
                  '  cat "$i.der" >"$o"\n'
                  '  exit 0;;\n'
                  'esac\n'
                  'for a; do f=$a; done\n'
                  'if grep -q BROKEN "$f"; then echo "unable to load certificate" >&2; exit 1; fi\n'
                  'case " $* " in *" -subject "*) echo "subject=CN = Example Root";; esac\n')
        self.stub("update-ca-certificates", 'echo "update-ca-certificates $*" >>"$CALLS"\n')

    def stub(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/sh\n" + body)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def run_install(self):
        env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}", CALLS=str(self.log),
                   CRUX_CA_DEST=str(self.dest))
        return subprocess.run(["sh", str(INSTALL), str(self.src)], env=env, capture_output=True, text=True)

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def installed(self):
        return sorted(path.name for path in self.dest.iterdir())

    def test_no_certificate_changes_nothing(self):
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), [])
        self.assertNotIn("update-ca-certificates", "\n".join(self.calls()))
        self.assertIn("no organisation CA", result.stdout)

    def test_missing_folder_changes_nothing(self):
        shutil.rmtree(self.src)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), [])
        self.assertEqual(self.calls(), [])

    def test_pem_certificate_is_installed_and_the_bundle_rebuilt(self):
        (self.src / "Org Root.crt").write_text(PEM)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), ["crux-org-Org_Root.crt"])
        self.assertEqual((self.dest / "crux-org-Org_Root.crt").read_text(), PEM)
        self.assertEqual(self.calls()[-1], "update-ca-certificates ")
        self.assertIn("Org Root.crt", result.stdout)
        self.assertIn("CN = Example Root", result.stdout)

    def test_windows_export_with_crlf_is_normalised(self):
        (self.src / "root.crt").write_bytes(PEM.replace("\n", "\r\n").encode())
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.dest / "crux-org-root.crt").read_text(), PEM)

    def test_bundle_file_is_split_into_one_certificate_per_file(self):
        (self.src / "chain.crt").write_text(PEM + PEM.replace("fake", "other"))
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), ["crux-org-chain-1.crt", "crux-org-chain-2.crt"])
        self.assertIn("other", (self.dest / "crux-org-chain-2.crt").read_text())

    def test_der_file_is_converted_with_openssl(self):
        (self.src / "root.cer").write_text("DER binary stand-in\n")
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), ["crux-org-root.crt"])
        self.assertIn("MIIBconverted", (self.dest / "crux-org-root.crt").read_text())
        self.assertIn("converted from DER", result.stdout)

    def test_der_in_a_crt_file_is_converted_and_the_file_left_unchanged(self):
        (self.src / "root.crt").write_bytes(b"DER binary stand-in\n")
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), ["crux-org-root.crt"])
        self.assertIn("-----BEGIN CERTIFICATE-----", (self.dest / "crux-org-root.crt").read_text())
        self.assertEqual((self.src / "root.crt").read_bytes(), b"DER binary stand-in\n")

    def test_der_with_data_after_the_certificate_stops_the_build(self):
        # openssl converts the first DER object and ignores the rest; the script must not install part of a file.
        (self.src / "a-good.crt").write_text(PEM)
        (self.src / "root.crt").write_text("DER binary stand-in\nsecond object\n")
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("root.crt", result.stderr)
        self.assertIn("exactly one DER certificate", result.stderr)
        self.assertEqual(self.installed(), [])
        self.assertNotIn("update-ca-certificates", "\n".join(self.calls()))

    def test_other_binary_content_names_pkcs7_and_pkcs12(self):
        (self.src / "root.crt").write_bytes(b"\x30\x82 not a certificate")
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PKCS#7", result.stderr)
        self.assertIn("PKCS#12", result.stderr)
        self.assertEqual(self.installed(), [])

    def test_der_file_is_skipped_without_openssl(self):
        (self.src / "root.crt").write_text("DER binary stand-in\n")
        env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}", CALLS=str(self.log),
                   CRUX_CA_DEST=str(self.dest), CRUX_CA_OPENSSL="crux-no-such-openssl")
        result = subprocess.run(["sh", str(INSTALL), str(self.src)], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), [])
        self.assertIn("skipped root.crt", result.stderr)
        self.assertIn("openssl x509 -inform der", result.stderr)

    def test_neither_pem_nor_der_stops_the_build(self):
        (self.src / "root.crt").write_bytes(b"\x00\x01 not a certificate")
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("root.crt", result.stderr)
        self.assertIn("neither a PEM nor a DER certificate", result.stderr)
        self.assertEqual(self.installed(), [])
        self.assertNotIn("update-ca-certificates", "\n".join(self.calls()))

    def test_unparsable_certificate_stops_the_build(self):
        (self.src / "root.crt").write_text(PEM.replace("fakebase64data", "BROKEN"))
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("root.crt", result.stderr)
        self.assertIn("not a valid certificate", result.stderr)
        self.assertEqual(self.installed(), [])

    def test_private_key_stops_the_build_and_is_never_installed(self):
        (self.src / "root.crt").write_text(PEM + "-----BEGIN PRIVATE KEY-----\nMIIE\n-----END PRIVATE KEY-----\n")
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("private key", result.stderr)
        self.assertEqual(self.installed(), [])

    def test_bad_file_installs_nothing_even_next_to_a_good_one(self):
        (self.src / "a-good.crt").write_text(PEM)
        (self.src / "b-bad.crt").write_text("hello\n")
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.installed(), [])
        self.assertNotIn("update-ca-certificates", "\n".join(self.calls()))

    def test_pem_and_cer_extensions_are_installed_as_crt(self):
        (self.src / "a.pem").write_text(PEM)
        (self.src / "b.cer").write_text(PEM)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), ["crux-org-a.crt", "crux-org-b.crt"])

    def test_same_name_with_two_extensions_keeps_both(self):
        (self.src / "root.crt").write_text(PEM)
        (self.src / "root.pem").write_text(PEM.replace("fake", "other"))
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), ["crux-org-root.crt", "crux-org-root_pem.crt"])

    def test_other_file_types_are_skipped_with_a_message(self):
        (self.src / "root.der").write_text("DER binary stand-in\n")
        (self.src / "notes.txt").write_text(PEM)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.installed(), [])
        self.assertIn("skipped root.der", result.stderr)
        self.assertIn("skipped notes.txt", result.stderr)
        self.assertIn(".crt, .pem or .cer", result.stderr)
        self.assertNotIn("update-ca-certificates", "\n".join(self.calls()))


@unittest.skipUnless(shutil.which("openssl"), "openssl is not installed")
class InstallScriptRealOpensslTest(unittest.TestCase):
    """The same script with the real openssl and a throw-away self-signed certificate."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.src = self.tmp / "certs"
        self.dest = self.tmp / "dest"
        bin_dir = self.tmp / "bin"
        for folder in (self.src, self.dest, bin_dir):
            folder.mkdir()
        update = bin_dir / "update-ca-certificates"
        update.write_text("#!/bin/sh\nexit 0\n")
        update.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}", CRUX_CA_DEST=str(self.dest))
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=Crux Test Root", "-keyout", str(self.tmp / "key.pem"),
                        "-out", str(self.tmp / "root.pem")], check=True, capture_output=True)

    def run_install(self):
        return subprocess.run(["sh", str(INSTALL), str(self.src)], env=self.env, capture_output=True, text=True)

    def test_pem_is_installed(self):
        shutil.copy(self.tmp / "root.pem", self.src / "root.crt")
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in self.dest.iterdir()), ["crux-org-root.crt"])
        self.assertIn("Crux Test Root", result.stdout)

    def test_der_is_converted(self):
        subprocess.run(["openssl", "x509", "-in", str(self.tmp / "root.pem"), "-outform", "der",
                        "-out", str(self.src / "root.cer")], check=True, capture_output=True)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in self.dest.iterdir()), ["crux-org-root.crt"])
        self.assertIn("-----BEGIN CERTIFICATE-----", (self.dest / "crux-org-root.crt").read_text())
        self.assertIn("Crux Test Root", result.stdout)

    def der(self, name):
        path = self.tmp / name
        subprocess.run(["openssl", "x509", "-in", str(self.tmp / "root.pem"), "-outform", "der", "-out", str(path)],
                       check=True, capture_output=True)
        return path.read_bytes()

    def assert_refused(self, result, needle):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(needle, result.stderr)
        self.assertEqual(list(self.dest.iterdir()), [])

    def test_der_in_a_crt_file_is_converted_and_the_file_left_unchanged(self):
        data = self.der("root.der")
        (self.src / "root.crt").write_bytes(data)
        result = self.run_install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-----BEGIN CERTIFICATE-----", (self.dest / "crux-org-root.crt").read_text())
        self.assertIn("converted from DER", result.stdout)
        self.assertEqual((self.src / "root.crt").read_bytes(), data)

    def test_two_der_certificates_in_one_file_are_refused(self):
        data = self.der("root.der")
        (self.src / "root.crt").write_bytes(data + data)
        self.assert_refused(self.run_install(), "exactly one DER certificate")

    def test_der_with_trailing_bytes_is_refused(self):
        (self.src / "root.crt").write_bytes(self.der("root.der") + b"junk")
        self.assert_refused(self.run_install(), "exactly one DER certificate")

    def test_pkcs7_bundle_is_refused(self):
        subprocess.run(["openssl", "crl2pkcs7", "-nocrl", "-certfile", str(self.tmp / "root.pem"), "-outform", "der",
                        "-out", str(self.src / "root.crt")], check=True, capture_output=True)
        self.assert_refused(self.run_install(), "PKCS#7")

    def test_pkcs12_file_is_refused_and_its_key_never_installed(self):
        made = subprocess.run(["openssl", "pkcs12", "-export", "-in", str(self.tmp / "root.pem"), "-inkey",
                               str(self.tmp / "key.pem"), "-passout", "pass:test", "-out", str(self.src / "root.crt")],
                              capture_output=True)
        if made.returncode != 0:
            self.skipTest("this openssl cannot write PKCS#12")
        self.assert_refused(self.run_install(), "PKCS#12")

    def test_random_bytes_are_refused(self):
        (self.src / "root.cer").write_bytes(b"\x00\x01" + bytes(range(2, 64)))
        result = self.run_install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("neither a PEM nor a DER certificate", result.stderr)
        self.assertEqual(list(self.dest.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
