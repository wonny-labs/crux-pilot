"""The web asset packager: a built folder to firmware data, headers, hashes, budgets and checks.

These tests need no compiler and no Zephyr, except the CMake ones, which need only cmake.
"""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
PACKAGER = REPO / "components/web/tools/web_assets.py"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


web_routes = load("web_routes_for_assets", REPO / "components/web/tools/web_routes.py")
web_assets = load("web_assets_under_test", PACKAGER)

INDEX = b'<!doctype html><html><head><link rel="stylesheet" href="/style.css"></head><body>' + \
    b'<div id="app"></div><script src="/app-Ab12Cd34.js"></script></body></html>\n'
SCRIPT = b"export const hello = () => 'hello, hello, hello, hello, hello, hello, hello';\n" * 20
STYLE = b"body { margin: 0; font-family: sans-serif; }\n" * 10
# Not compressible: gzip would only add its own header.
def noise(length, state=1):
    data = bytearray()
    for _ in range(length):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        data.append(state >> 16 & 0xFF)
    return bytes(data)


PNG = noise(300)


def make_site(root, extra=None):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    files = {"index.html": INDEX, "app-Ab12Cd34.js": SCRIPT, "style.css": STYLE, "logo.png": PNG}
    files.update(extra or {})
    for name, content in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode())
    return root


def package(site, out, *arguments):
    """Run the packager as the build does. Returns the completed process."""
    command = [sys.executable, str(PACKAGER), str(site), "--c-out", f"{out}/assets.c",
               "--hash-list", f"{out}/assets.json", "--response-deadline-ms", "3000", *arguments]
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)


def embedded(source):
    """The bytes of each data array in a generated source, by array index."""
    arrays = {}
    for match in re.finditer(r"crux_web_asset_data_(\d+)\[\] = \{(.*?)\};", source, re.DOTALL):
        arrays[int(match.group(1))] = bytes(int(token, 16) for token in re.findall(r"0x([0-9a-f]{2})", match.group(2)))
    return arrays


def header_lists(source):
    """The header initialiser of each asset, as a list of (name, value or macro) by index."""
    lists = {}
    for match in re.finditer(r"crux_web_asset_headers_(\d+)\[\] = \{(.*?)\n\};", source, re.DOTALL):
        entries = []
        for line in match.group(2).splitlines():
            line = line.strip().rstrip(",")
            pair = re.fullmatch(r'\{"([^"]+)", "([^"]*)"\}', line)
            if pair:
                entries.append(pair.groups())
            elif line:
                entries.append((line, None))
        lists[int(match.group(1))] = entries
    return lists


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.site = make_site(self.root / "site")
        self.out = self.root / "out"
        self.out.mkdir()

    def ok(self, *arguments, site=None):
        result = package(site or self.site, self.out, *arguments)
        self.assertEqual(result.returncode, 0, result.stdout)
        return result

    def fails(self, reason, *arguments, site=None):
        result = package(site or self.site, self.out, *arguments)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertRegex(result.stdout, reason)
        self.assertFalse((self.out / "assets.c").exists(), "a failed run must leave no source")
        return result

    def generated(self):
        return (self.out / "assets.c").read_text(encoding="utf-8")

    def hashes(self):
        return json.loads((self.out / "assets.json").read_text(encoding="utf-8"))


class Encoding(Fixture):
    def test_embeds_gzip_that_decodes_to_the_file(self):
        self.ok()
        source = self.generated()
        listing = self.hashes()
        data = embedded(source)
        self.assertEqual(len(data), len(listing["assets"]))
        for index, asset in enumerate(listing["assets"]):
            served = data[index]
            self.assertEqual(len(served), asset["served_bytes"])
            self.assertEqual(hashlib.sha256(served).hexdigest(), asset["served_sha256"])
            original = (self.site / asset["file"]).read_bytes()
            if asset["encoding"] == "gzip":
                self.assertEqual(gzip.decompress(served), original, asset["path"])
            else:
                self.assertEqual(served, original, asset["path"])

    def test_text_is_compressed_and_what_gzip_cannot_shrink_is_not(self):
        self.ok()
        encodings = {asset["file"]: asset["encoding"] for asset in self.hashes()["assets"]}
        self.assertEqual(encodings["app-Ab12Cd34.js"], "gzip")
        self.assertEqual(encodings["index.html"], "gzip")
        self.assertEqual(encodings["logo.png"], "identity")

    def test_a_file_that_is_already_gzip_is_not_compressed_twice(self):
        packed = gzip.compress(SCRIPT, compresslevel=9, mtime=0)
        make_site(self.site, {"pre-Zz11Yy22.js": packed})
        self.ok()
        asset = next(a for a in self.hashes()["assets"] if a["file"] == "pre-Zz11Yy22.js")
        self.assertEqual(asset["encoding"], "gzip")
        self.assertTrue(asset["precompressed"])
        self.assertEqual(asset["served_bytes"], len(packed))
        self.assertEqual(asset["sha256"], hashlib.sha256(packed).hexdigest())
        index = [a["file"] for a in self.hashes()["assets"]].index("pre-Zz11Yy22.js")
        self.assertEqual(embedded(self.generated())[index], packed)
        self.assertIn(("Content-Encoding", "gzip"), header_lists(self.generated())[index])
        self.assertNotIn("precompressed", next(a for a in self.hashes()["assets"] if a["file"] == "style.css"))

    def test_a_gzip_file_with_anything_after_its_member_is_an_error(self):
        first = gzip.compress(b"console.log(1)\n" * 50, mtime=0)
        for label, content in {
            "a second member holding a key": first + gzip.compress(PRIVATE_KEY.encode(), mtime=0),
            "a second harmless member": first + gzip.compress(b"x", mtime=0),
            "bytes that are not gzip": first + b"trailing bytes that are not gzip",
            "a single zero byte": first + b"\x00",
        }.items():
            with self.subTest(label):
                make_site(self.site, {"app.js": content})
                self.fails(r"app\.js.*(more than one|trailing|not valid gzip)")
        make_site(self.site, {"app.js": first})
        self.ok()

    def test_a_truncated_gzip_member_is_an_error(self):
        first = gzip.compress(b"console.log(1)\n" * 50, mtime=0)
        make_site(self.site, {"app.js": first[:-6]})
        self.fails(r"app\.js.*not valid gzip")

    def test_a_gzip_bomb_is_refused_at_the_cap_with_memory_bounded(self):
        import tracemalloc
        # Four times the cap, so that unbounded decoding would peak at four times it and the margin
        # to the bound below is a whole cap, not a few MiB.
        bomb = gzip.compress(bytes(4 * web_assets.DECODE_MAX), mtime=0)
        self.assertLess(len(bomb), 1024 * 1024)
        errors = []
        tracemalloc.start()
        try:
            self.assertIsNone(web_assets.pregzipped(bomb, "bomb.js", errors))
            peak = tracemalloc.get_traced_memory()[1]
        finally:
            tracemalloc.stop()
        self.assertEqual(len(errors), 1)
        self.assertIn("bomb.js", errors[0])
        # Bounded decoding builds the output buffer and may copy it once, so about twice the cap, which
        # was 32.1 MiB measured on CPython 3.12 on Linux. Unbounded would reach the bomb's 64 MiB, so
        # 48 MiB is a bound with a margin of 16 MiB on each side.
        self.assertLess(peak, 3 * web_assets.DECODE_MAX)
        make_site(self.site, {"bomb.js": bomb})
        self.fails(r"bomb\.js.*(not valid gzip|decodes to over)")
        self.assertEqual(web_assets.DECODE_MAX, 16 * 1024 * 1024)

    def test_a_gzip_file_that_decodes_to_exactly_the_cap_is_accepted_and_one_more_byte_is_not(self):
        at_cap = gzip.compress(bytes(web_assets.DECODE_MAX), mtime=0)
        errors = []
        self.assertTrue(web_assets.pregzipped(at_cap, "cap.js", errors), errors)
        self.assertEqual(errors, [])
        over = gzip.compress(bytes(web_assets.DECODE_MAX + 1), mtime=0)
        self.assertIsNone(web_assets.pregzipped(over, "over.js", errors))
        self.assertEqual(len(errors), 1)

    def test_a_precompressed_file_is_still_scanned_and_must_be_valid(self):
        make_site(self.site, {"bad.js": b"\x1f\x8bnot really gzip"})
        self.fails(r"bad\.js.*not valid gzip")
        make_site(self.site, {"bad.js": "1", "key.txt": gzip.compress(PRIVATE_KEY.encode())})
        self.fails(r"key\.txt.*a PEM private key")

    def test_content_encoding_header_follows_the_encoding(self):
        self.ok()
        lists = header_lists(self.generated())
        for index, asset in enumerate(self.hashes()["assets"]):
            names = [name for name, _ in lists[index]]
            if asset["encoding"] == "gzip":
                self.assertIn(("Content-Encoding", "gzip"), lists[index])
            else:
                self.assertNotIn("Content-Encoding", names)

    def test_output_is_deterministic(self):
        self.ok()
        first = (self.generated(), (self.out / "assets.json").read_text(encoding="utf-8"))
        shutil.rmtree(self.out)
        self.out.mkdir()
        os.utime(self.site / "style.css", (1, 1))
        self.ok()
        self.assertEqual(first, (self.generated(), (self.out / "assets.json").read_text(encoding="utf-8")))


class Headers(Fixture):
    def headers_of(self, path):
        listing = self.hashes()["assets"]
        index = next(i for i, asset in enumerate(listing) if asset["path"] == path)
        return header_lists(self.generated())[index]

    def test_content_types(self):
        self.ok()
        cases = {"/index.html": "text/html; charset=utf-8", "/app-Ab12Cd34.js": "text/javascript; charset=utf-8",
                 "/style.css": "text/css; charset=utf-8", "/logo.png": "image/png"}
        for path, content_type in cases.items():
            self.assertIn(("Content-Type", content_type), self.headers_of(path), path)

    def test_every_asset_carries_the_security_headers_and_a_cache_header(self):
        self.ok()
        for asset_headers in header_lists(self.generated()).values():
            names = [name for name, _ in asset_headers]
            self.assertEqual(names.count("Content-Type"), 1)
            self.assertEqual(names.count("Cache-Control"), 1)
            self.assertIn("CRUX_WEB_ASSETS_SECURITY_HEADERS", names)
            self.assertLessEqual(len(names) + 2, web_assets.RESPONSE_HEADERS_MAX)

    def test_cache_policy(self):
        self.ok()
        self.assertIn(("Cache-Control", "no-cache"), self.headers_of("/index.html"))
        self.assertIn(("Cache-Control", "no-cache"), self.headers_of("/style.css"))
        self.assertIn(("Cache-Control", "max-age=31536000, immutable"), self.headers_of("/app-Ab12Cd34.js"))

    def test_fingerprint_rule(self):
        fingerprinted = web_assets.fingerprinted
        for name in ("app-Ab12Cd34.js", "index-DiwrgTda.js", "chunk-2NRPQNBA.js", "main.3f2a9c1e.css",
                     "vendor-0123456789abcdef.js"):
            self.assertTrue(fingerprinted(name), name)
        for name in ("index.html", "style.css", "bootstrap.min.js", "main-bundle.js", "my-long-name.js",
                     "polyfills-legacy.js", "favicon.svg", "app-Ab12.js"):
            self.assertFalse(fingerprinted(name), name)

    def test_style_policy_is_a_setting_with_two_values(self):
        self.ok()
        self.assertIn("#define CRUX_ASSETS_INLINE_STYLES 0", self.generated())
        shutil.rmtree(self.out)
        self.out.mkdir()
        self.ok("--style", "strict")
        self.assertIn("#define CRUX_ASSETS_INLINE_STYLES 0", self.generated())
        shutil.rmtree(self.out)
        self.out.mkdir()
        self.ok("--style", "inline")
        self.assertIn("#define CRUX_ASSETS_INLINE_STYLES 1", self.generated())
        self.assertEqual(self.hashes()["style"], "inline")

    def test_no_free_form_policy(self):
        for value in ("default-src *", "unsafe-inline", "'unsafe-inline'", "Strict", ""):
            with self.subTest(value=value):
                result = package(self.site, self.out, "--style", value)
                self.assertNotEqual(result.returncode, 0)
        parser_help = subprocess.run([sys.executable, str(PACKAGER), "--help"], stdout=subprocess.PIPE,
                                     text=True, check=False).stdout
        self.assertNotIn("--csp", parser_help)
        self.assertNotIn("--policy", parser_help)

    def test_policies_in_the_private_header_hold_the_decisions(self):
        header = (REPO / "components/web/assets/web_assets_internal.h").read_text(encoding="utf-8")
        header = re.sub(r"\\\n", " ", header)

        def policy(name):
            line = re.search(r"#define " + name + r"\s+([^\n]*)", header).group(1)
            return "".join(re.findall(r'"([^"]*)"', line))

        strict, inline = policy("CRUX_WEB_ASSETS_POLICY_STRICT"), policy("CRUX_WEB_ASSETS_POLICY_INLINE")
        for text in (strict, inline):
            self.assertIn("script-src 'self'", text)
            self.assertIn("frame-ancestors 'none'", text)
            self.assertNotIn("unsafe-eval", text)
            self.assertNotIn("script-src 'self' 'unsafe-inline'", text)
            self.assertNotRegex(text, r"https?:|\*")
        self.assertNotIn("'unsafe-inline'", strict)
        self.assertEqual(inline.count("'unsafe-inline'"), 1)
        self.assertRegex(inline, r"style-src 'self' 'unsafe-inline'")
        # The exact values, so that a source such as data: or blob: added to any directive fails here.
        self.assertEqual(strict, "default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; "
                                 "form-action 'self'; frame-ancestors 'none'")
        self.assertEqual(inline, "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                                 "base-uri 'self'; form-action 'self'; frame-ancestors 'none'")


class Routes(Fixture):
    def test_one_public_get_route_per_asset_and_root_serves_index(self):
        self.ok()
        source = self.generated()
        routes = web_routes.parse_source(source, "assets.c")
        paths = sorted(route.path for route in routes)
        self.assertEqual(paths, sorted(["/", "/index.html", "/app-Ab12Cd34.js", "/style.css", "/logo.png"]))
        for route in routes:
            self.assertEqual(route.access_class, "public")
            self.assertEqual(route.methods, ("CRUX_WEB_GET",))
        # No fallback and no wildcard: hash routing needs no deep-link answer.
        self.assertFalse(any(route.wildcard for route in routes))
        resources = web_routes.build_resources(routes)
        self.assertEqual(len(resources), len(routes))

    def test_root_and_index_share_one_embedded_copy(self):
        self.ok()
        source = self.generated()
        self.assertEqual(len(embedded(source)), 4)
        self.assertEqual(len(web_routes.parse_source(source, "assets.c")), 5)

    def test_nested_folders_keep_their_paths(self):
        make_site(self.site, {"assets/icons/up.svg": "<svg xmlns='http://www.w3.org/2000/svg'/>"})
        self.ok()
        self.assertIn("/assets/icons/up.svg", [asset["path"] for asset in self.hashes()["assets"]])

    def test_the_review_list_names_the_bundle_once(self):
        self.ok()
        routes = web_routes.parse_sources([], bundle=[str(self.out / "assets.c")])
        self.assertEqual(web_routes.review_lines(routes), ["public GET @assets"])

    def test_bundle_without_review_entry_fails_and_stale_entry_fails(self):
        self.ok()
        routes = web_routes.parse_sources([], bundle=[str(self.out / "assets.c")])
        listing = self.root / "reviewed.txt"
        listing.write_text("public POST /api/v1/session\n", encoding="utf-8")
        errors = web_routes.review_errors(routes, listing)
        self.assertIn("not reviewed: public GET @assets", errors)
        listing.write_text("public GET @assets\n", encoding="utf-8")
        self.assertEqual(web_routes.review_errors(routes, listing), [])
        self.assertEqual(web_routes.review_errors([], listing), ["reviewed, but no such route: public GET @assets"])

    GOOD = ('CRUX_WEB_ROUTE(crux_web_asset_route_0000, "/a", CRUX_WEB_GET, CRUX_WEB_PUBLIC, '
            'crux_web_assets_serve, &crux_web_asset_table[0]);')

    # What the packager writes before its first statement, which a bundle must start with.
    PREAMBLE = '#define CRUX_ASSETS_INLINE_STYLES 0\n#include "web_assets_internal.h"\n'

    def refused(self, text, preamble=None):
        bad = self.root / "bad.c"
        bad.write_text((self.PREAMBLE if preamble is None else preamble) + text, encoding="utf-8")
        with self.assertRaises(web_routes.RouteError):
            web_routes.parse_sources([], bundle=[str(bad)])

    def test_a_bundle_source_may_hold_only_public_get_asset_routes(self):
        bad = self.root / "bad.c"
        bad.write_text(self.PREAMBLE + self.GOOD, encoding="utf-8")
        # Even a well-formed route is refused in a file that the packager did not write.
        with self.assertRaises(web_routes.RouteError):
            web_routes.parse_sources([], bundle=[str(bad)])
        for text in (self.GOOD.replace("CRUX_WEB_GET", "CRUX_WEB_POST"),
                     self.GOOD.replace("CRUX_WEB_PUBLIC", "CRUX_WEB_SESSION"),
                     self.GOOD.replace("CRUX_WEB_PUBLIC", '"x.read"'),
                     self.GOOD.replace("crux_web_asset_route_0000", "mine"),
                     self.GOOD.replace('"/a"', '"/a/{id}"')):
            with self.subTest(text=text):
                self.refused(text)

    def test_a_bundle_route_must_use_the_asset_handler_and_a_table_entry(self):
        for label, text in {
            "a foreign handler": self.GOOD.replace("crux_web_assets_serve", "factory_reset"),
            "a handler that only starts like it": self.GOOD.replace("crux_web_assets_serve",
                                                                    "crux_web_assets_serve_all"),
            "a foreign context": self.GOOD.replace("&crux_web_asset_table[0]", "NULL"),
            "another table": self.GOOD.replace("crux_web_asset_table", "other_table"),
            "a context that is not an element": self.GOOD.replace("&crux_web_asset_table[0]",
                                                                  "crux_web_asset_table"),
            "an index that is not a number": self.GOOD.replace("[0]", "[x]"),
            "text after the element": self.GOOD.replace("[0]", "[1] + x"),
            "an element and a cast": self.GOOD.replace("&crux", "(void *)&crux"),
            "the body-limit form": self.GOOD.replace("CRUX_WEB_ROUTE(", "CRUX_WEB_ROUTE_BODY(").replace(
                ");", ", 512u);"),
        }.items():
            with self.subTest(label):
                self.refused(text)

    def test_a_bundle_holds_no_preprocessor_line_but_the_two_generated_ones(self):
        route, pre = self.GOOD, self.PREAMBLE
        for label, text in {
            "the rename of the handler": pre + "#define crux_web_assets_serve do_factory_reset\n" + route,
            "an include before the route": pre + '#include "evil.h"\n' + route,
            "a rename of the table": pre + "#define crux_web_asset_table secrets\n" + route,
            "a rename of the route macro": pre + "#define CRUX_WEB_ROUTE(...)\n" + route,
            "a directive hidden by a continuation": pre + "#define crux_web_assets_serve \\\n  do_factory_reset\n" + route,
            "a directive with a space after the hash": pre + "  #  define crux_web_assets_serve other\n" + route,
            "a pragma": pre + "#pragma weak crux_web_assets_serve\n" + route,
            "a conditional": pre + "#if 0\n#endif\n" + route,
            "a second include": pre + '#include "web_assets_internal.h"\n' + route,
            "another style define": pre + "#define CRUX_ASSETS_INLINE_STYLES 1\n" + route,
            "a directive after the route": pre + route + "\n#define crux_web_assets_serve other\n",
            "a define with another value": '#define CRUX_ASSETS_INLINE_STYLES 2\n#include "web_assets_internal.h"\n' + route,
            "an include of another file": '#define CRUX_ASSETS_INLINE_STYLES 0\n#include "evil.h"\n' + route,
            "no preamble": route,
            "the preamble the wrong way round": '#include "web_assets_internal.h"\n#define CRUX_ASSETS_INLINE_STYLES 0\n' + route,
        }.items():
            with self.subTest(label):
                self.refused(text, preamble="")

    def test_a_bundle_holds_no_other_statement(self):
        for label, extra in {
            "a variable": "int hidden;\n",
            "a function": "int hidden(void) { return 1; }\n",
            "a typedef": "typedef int route_t;\n",
            "an extern": "extern int secrets;\n",
            "a second table": "const crux_web_asset_t other_table[] = {{\"/x\", 0, 0, 0, 0u}};\n",
            "a data array that holds code": "static const uint8_t crux_web_asset_data_0000[] = {1 + hidden()};\n",
            "a header list with a call": 'static const crux_web_header_t crux_web_asset_headers_0000[] = {{"A", hidden()},};\n',
            "a table entry with a foreign member": 'const crux_web_asset_t crux_web_asset_table[] = {{"/x", other, 1, h, 1u},};\n',
            "an asm statement": 'asm("nop");\n',
        }.items():
            with self.subTest(label):
                self.refused(extra + self.GOOD)
                self.refused(self.GOOD + extra)

    def test_the_generated_form_passes_the_structure_check(self):
        self.ok()
        routes = web_routes.parse_sources([], bundle=[str(self.out / "assets.c")])
        self.assertEqual(len(routes), 5)
        self.ok("--style", "inline")
        self.assertEqual(len(web_routes.parse_sources([], bundle=[str(self.out / "assets.c")])), 5)

    def test_a_bundle_cannot_take_a_path_under_the_api(self):
        for path in ("/api", "/api/v1/reset", "/api/v1/users/admin.json"):
            with self.subTest(path=path):
                self.refused(self.GOOD.replace('"/a"', f'"{path}"'))

    def test_the_repro_of_a_foreign_route_is_refused_by_the_tool(self):
        bad = self.root / "b.c"
        bad.write_text('CRUX_WEB_ROUTE(crux_web_asset_route_9, "/reset", CRUX_WEB_GET, CRUX_WEB_PUBLIC, '
                       'factory_reset, NULL);\n', encoding="utf-8")
        listing = self.root / "rev.txt"
        listing.write_text("public GET @assets\n", encoding="utf-8")
        self.assertEqual(web_routes.main(["--bundle", str(bad), "--table", str(self.root / "t.c"),
                                          "--reviewed", str(listing)]), 1)
        self.assertFalse((self.root / "t.c").exists())

    # The reviewer's repros of the loose route pattern, named. Each is refused, by the equality with
    # what the packager writes, and not by a pattern.
    def test_repro_a_comma_declarator_after_a_route_hides_a_define(self):
        text = (self.PREAMBLE + 'CRUX_WEB_ROUTE(crux_web_asset_route_0, "/a", CRUX_WEB_GET, CRUX_WEB_PUBLIC, '
                'crux_web_assets_serve, &crux_web_asset_table[0])\n'
                '#define crux_web_assets_serve do_factory_reset\n, *crux_unused = (0);\n'
                'CRUX_WEB_ROUTE(crux_web_asset_route_1, "/reset", CRUX_WEB_GET, CRUX_WEB_PUBLIC, '
                'crux_web_assets_serve, &crux_web_asset_table[0]);\n')
        self.refused(text, preamble="")

    def test_repro_a_valid_route_followed_by_a_declaration(self):
        self.refused('CRUX_WEB_ROUTE(valid), hidden = {0}, *p = (0);\n', preamble="")

    def test_repro_a_directive_between_the_close_of_a_route_and_its_end(self):
        self.refused('CRUX_WEB_ROUTE(valid)\n#define crux_web_assets_serve evil\n );\n', preamble="")

    def test_repros_through_the_tool_and_a_good_file_through_the_same_door(self):
        self.ok()
        good = (self.out / "assets.c").read_text(encoding="utf-8")
        listing = self.root / "rev.txt"
        listing.write_text("public GET @assets\n", encoding="utf-8")
        bad = self.root / "b.c"
        bad.write_text(good.replace('CRUX_WEB_ROUTE(crux_web_asset_route_0001,',
                                    '#define crux_web_assets_serve do_factory_reset\nCRUX_WEB_ROUTE('
                                    'crux_web_asset_route_0001,'), encoding="utf-8")
        for path, status in ((self.out / "assets.c", 0), (bad, 1)):
            table = self.root / f"t{status}.c"
            self.assertEqual(web_routes.main(["--bundle", str(path), "--table", str(table),
                                              "--reviewed", str(listing)]), status, path)

    def test_a_bundle_that_is_not_text_is_refused(self):
        bad = self.root / "bad.c"
        bad.write_bytes(b"\xff\xfe\x00")
        with self.assertRaises(web_routes.RouteError):
            web_routes.parse_sources([], bundle=[str(bad)])

    def test_the_form_is_shared_so_the_writer_and_the_check_cannot_drift(self):
        form = load("web_assets_form_under_test", REPO / "components/web/tools/web_assets_form.py")
        self.ok()
        text = self.generated()
        self.assertIsNone(form.check_source(text))
        assets = [(a["path"], embedded(text)[i]) for i, a in enumerate(self.hashes()["assets"])]
        self.assertEqual(form.render_source("strict", assets)[0], text)

    def test_the_generated_source_is_refused_if_one_byte_of_form_changes(self):
        form = load("web_assets_form_under_test2", REPO / "components/web/tools/web_assets_form.py")
        self.ok()
        text = self.generated()
        for old, new in (("crux_web_assets_serve", "crux_web_assets_servf"),
                         ("CRUX_WEB_GET", "CRUX_WEB_PUT"), ("5u}", "6u}"), ("no-cache", "no-store"),
                         ("served as gzip", "served as identity"), ("\n", "\r\n"),
                         ("_table[1]);", "_table[1]) ;"), ("\n\nCRUX", "\n\n\nCRUX")):
            with self.subTest(old=old, new=new):
                self.assertIn(old, text)
                with self.assertRaises(form.FormError):
                    form.check_source(text.replace(old, new, 1) if old != "\n" else text.replace(old, new))
        with self.assertRaises(form.FormError):
            form.check_source(text + "\n")
        with self.assertRaises(form.FormError):
            form.check_source(text + "int x;\n")
        with self.assertRaises(form.FormError):
            form.check_source(text[:-1])

    def test_mutations_of_real_output_are_never_accepted_unless_they_equal_it(self):
        import random
        data_row = re.compile(r"^    0x[0-9a-f]{2}(, 0x[0-9a-f]{2})*,$")
        style_line = re.compile(r"^#define CRUX_ASSETS_INLINE_STYLES [01]$")

        def shape(text):
            """Every line but the data rows, with the style value left out: what a mutation must keep."""
            return [line for line in text.replace("\r", "<CR>").split("\n")
                    if not data_row.match(line) and not style_line.match(line)]

        junk = ["\n", "#", ";", "(", ")", '"', "\\", "{", "}", ",", " ", "\t", "\r", "*", "/", "&", "x", "0",
                "]", "[", "#define crux_web_assets_serve do_factory_reset\n", "#include \"evil.h\"\n",
                "); #define a b\n", ", hidden = {0}, *p = (0);", "\n#pragma once\n", "\\\n"]
        accepted = refused = 0
        for style in ("strict", "inline"):
            shutil.rmtree(self.out)
            self.out.mkdir()
            extra = make_site(self.root / f"site-{style}", {"assets/font-Qq11Ww22.woff2": noise(200, 3),
                                                            "data.json": '{"a": 1}'})
            self.assertEqual(package(extra, self.out, "--style", style).returncode, 0)
            canonical = self.generated()
            canonical_routes = [(r.name, r.path) for r in web_routes.parse_sources([], bundle=[str(self.out / "assets.c")])]
            rng = random.Random(189)
            lines = canonical.split("\n")
            for _ in range(700):
                kind = rng.randrange(10)
                if kind == 0:
                    position = rng.randrange(len(canonical) + 1)
                    mutated = canonical[:position] + rng.choice(junk) + canonical[position:]
                elif kind == 1:
                    position = rng.randrange(len(canonical))
                    mutated = canonical[:position] + canonical[position + 1:]
                elif kind == 2:
                    position = rng.randrange(len(canonical))
                    mutated = canonical[:position] + rng.choice(junk) + canonical[position + 1:]
                elif kind == 3:
                    number = rng.randrange(len(lines))
                    mutated = "\n".join(lines[:number] + [lines[number] + rng.choice(junk)] + lines[number + 1:])
                elif kind == 4:
                    number = rng.randrange(len(lines))
                    mutated = "\n".join(lines[:number] + [rng.choice(junk).strip("\n")] + lines[number:])
                elif kind == 5:
                    number = rng.randrange(len(lines))
                    mutated = "\n".join(lines[:number] + [lines[rng.randrange(len(lines))]] + lines[number:])
                elif kind == 6:
                    number = rng.randrange(len(lines))
                    mutated = "\n".join(lines[:number] + lines[number + 1:])
                elif kind == 7:
                    first, second = rng.randrange(len(lines)), rng.randrange(len(lines))
                    swapped = list(lines)
                    swapped[first], swapped[second] = swapped[second], swapped[first]
                    mutated = "\n".join(swapped)
                elif kind == 8:
                    mutated = canonical.replace("\n", "\r\n")
                else:
                    mutated = canonical + rng.choice(junk)
                path = self.root / "mutant.c"
                path.write_bytes(mutated.encode("utf-8"))
                try:
                    routes = web_routes.parse_sources([], bundle=[str(path)])
                except web_routes.RouteError:
                    refused += 1
                    continue
                accepted += 1
                # Only a data row or the style choice may differ, and the routes must be the same.
                self.assertEqual(shape(mutated), shape(canonical), mutated[:300])
                self.assertEqual([(r.name, r.path) for r in routes], canonical_routes)
        self.assertGreater(refused, 0.9 * (accepted + refused))
        self.assertGreater(refused, 1000)

    def test_the_generator_runs_on_the_generated_source(self):
        self.ok()
        listing = self.root / "reviewed.txt"
        listing.write_text("public GET @assets\n", encoding="utf-8")
        table = self.root / "table.c"
        other = self.root / "routes.c"
        other.write_text('CRUX_WEB_ROUTE(a, "/api/a", CRUX_WEB_GET, "a.read", h, NULL);', encoding="utf-8")
        status = web_routes.main([str(other), "--bundle", str(self.out / "assets.c"), "--table", str(table),
                                  "--reviewed", str(listing)])
        self.assertEqual(status, 0)
        text = table.read_text(encoding="utf-8")
        self.assertEqual(text.count("crux_web_route_crux_web_asset_route_"), 5 * 2)
        listing.write_text("", encoding="utf-8")
        self.assertEqual(web_routes.main([str(other), "--bundle", str(self.out / "assets.c"),
                                          "--table", str(self.root / "t2.c"), "--reviewed", str(listing)]), 1)


class Hashes(Fixture):
    def test_hash_list_is_sorted_complete_and_correct(self):
        self.ok()
        listing = self.hashes()
        self.assertEqual(listing["format"], "crux-web-assets/1")
        paths = [asset["path"] for asset in listing["assets"]]
        self.assertEqual(paths, sorted(paths))
        self.assertNotIn("/", paths[0][1:] and "")  # a path is rooted
        for asset in listing["assets"]:
            content = (self.site / asset["file"]).read_bytes()
            self.assertEqual(asset["sha256"], hashlib.sha256(content).hexdigest())
            self.assertEqual(asset["bytes"], len(content))
            self.assertRegex(asset["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(listing["totals"]["files"], 4)
        self.assertEqual(listing["totals"]["served_bytes"], sum(asset["served_bytes"] for asset in listing["assets"]))

    def test_hash_list_holds_no_file_content(self):
        secret_looking = "unique-marker-" + "x" * 40
        make_site(self.site, {"note.txt": secret_looking})
        self.ok()
        self.assertNotIn(secret_looking, (self.out / "assets.json").read_text(encoding="utf-8"))


class Budget(Fixture):
    def served(self):
        return {asset["file"]: asset["served_bytes"] for asset in self.hashes()["assets"]}

    def test_states_the_largest_asset_that_fits(self):
        result = self.ok("--response-deadline-ms", "3000", "--client-rate", "32768")
        self.assertIn("largest asset that fits: 98304 bytes", result.stdout)
        self.assertEqual(self.hashes()["budget"]["max_asset_bytes"], 98304)
        self.assertEqual(self.hashes()["budget"]["response_deadline_ms"], 3000)
        self.assertEqual(self.hashes()["budget"]["client_rate_bytes_per_second"], 32768)

    def test_largest_asset_follows_the_deadline_and_the_rate(self):
        self.assertEqual(web_assets.largest_asset(3000, 32768), 98304)
        self.assertEqual(web_assets.largest_asset(1500, 32768), 49152)
        self.assertEqual(web_assets.largest_asset(10, 100), 1)
        self.assertEqual(web_assets.largest_asset(600000, 1000000), 600000000)

    def test_an_asset_at_the_limit_fits_and_one_byte_more_does_not(self):
        self.ok()
        size = self.served()["logo.png"]
        shutil.rmtree(self.out)
        self.out.mkdir()
        # The deadline and rate that allow exactly `size` bytes: 1000 ms at `size` bytes a second.
        self.ok("--response-deadline-ms", "1000", "--client-rate", str(size))
        shutil.rmtree(self.out)
        self.out.mkdir()
        self.fails(r"logo\.png.*exceeds", "--response-deadline-ms", "1000", "--client-rate", str(size - 1))

    def test_names_the_asset_over_the_limit_with_the_numbers(self):
        result = self.fails(r"\/logo\.png", "--response-deadline-ms", "1000", "--client-rate", "100")
        self.assertIn("largest asset that fits: 100 bytes", result.stdout)
        self.assertIn("CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS", result.stdout)

    def test_file_count(self):
        self.fails(r"4 files.*at most 3", "--max-files", "3")
        self.ok("--max-files", "4")

    def test_the_root_alias_is_not_a_file(self):
        # index.html is served at / and at /index.html, and is one file.
        self.ok("--max-files", "4")
        self.assertEqual(self.hashes()["totals"]["routes"], 5)

    def test_total_size(self):
        total = sum(asset["served_bytes"] for asset in (package(self.site, self.out) and self.hashes())["assets"])
        shutil.rmtree(self.out)
        self.out.mkdir()
        self.ok("--max-total-bytes", str(total))
        shutil.rmtree(self.out)
        self.out.mkdir()
        self.fails(r"compressed total", "--max-total-bytes", str(total - 1))

    def test_defaults_are_declared(self):
        self.assertEqual(web_assets.DEFAULT_MAX_FILES, 32)
        self.assertEqual(web_assets.DEFAULT_MAX_TOTAL_BYTES, 393216)
        self.assertEqual(web_assets.DEFAULT_CLIENT_RATE, 32768)

    def test_a_bad_budget_argument_fails(self):
        for arguments in (["--max-files", "0"], ["--max-total-bytes", "-1"], ["--client-rate", "0"],
                          ["--response-deadline-ms", "0"], ["--max-files", "many"]):
            with self.subTest(arguments=arguments):
                self.assertNotEqual(package(self.site, self.out, *arguments).returncode, 0)

    def test_the_deadline_is_required(self):
        result = subprocess.run([sys.executable, str(PACKAGER), str(self.site), "--c-out", f"{self.out}/a.c"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("response-deadline-ms", result.stdout)

    def test_a_large_incompressible_asset_over_the_default_limit_fails(self):
        make_site(self.site, {"big-Zz99Yy88.js": noise(110000, 12345)})
        result = self.fails(r"big-Zz99Yy88\.js")
        self.assertIn("98304", result.stdout)


class Folder(Fixture):
    def test_missing_folder(self):
        self.fails(r"does not exist", site=self.root / "nowhere")

    def test_a_file_is_not_a_folder(self):
        (self.root / "plain").write_text("x", encoding="utf-8")
        self.fails(r"not a folder", site=self.root / "plain")

    def test_empty_folder(self):
        (self.root / "empty").mkdir()
        self.fails(r"index\.html", site=self.root / "empty")

    def test_index_is_required_at_the_top(self):
        (self.site / "index.html").unlink()
        (self.site / "sub").mkdir()
        (self.site / "sub/index.html").write_bytes(INDEX)
        self.fails(r"index\.html")

    def test_what_the_server_cannot_serve_by_name(self):
        for name in ("has space.js", "tilde~.js", "café.js", "a%20b.js", "semi;colon.js", "a@b.js"):
            with self.subTest(name=name):
                shutil.rmtree(self.site)
                make_site(self.site, {name: "1"})
                if name == "tilde~.js":
                    self.ok()  # ~ is an unreserved character
                    shutil.rmtree(self.out)
                    self.out.mkdir()
                else:
                    self.fails(r"cannot be a URL path")

    def test_hidden_files_are_refused(self):
        make_site(self.site, {".DS_Store": "x"})
        self.fails(r"hidden")

    def test_unknown_types_are_refused_with_a_reason(self):
        for name in ("app.js.map", "key.pem", "cert.crt", "secrets.env", "notes.md", "a.wasm", "noext"):
            with self.subTest(name=name):
                shutil.rmtree(self.site)
                make_site(self.site, {name: "x"})
                self.fails(r"not a type this packager serves")

    def test_symlinks_are_refused(self):
        outside = self.root / "outside.txt"
        outside.write_text("secret", encoding="utf-8")
        os.symlink(outside, self.site / "link.txt")
        self.fails(r"symbolic link")

    def test_symlinked_folders_are_refused(self):
        other = self.root / "other"
        make_site(other)
        os.symlink(other, self.site / "linked")
        self.fails(r"symbolic link")

    def test_paths_that_differ_only_in_case_are_refused(self):
        make_site(self.site, {"Readme.txt": "a", "README.txt": "b"})
        if len({name.lower() for name in os.listdir(self.site)}) == len(os.listdir(self.site)):
            self.skipTest("this file system folds case")
        self.fails(r"differ only in case")

    def test_long_paths_are_refused(self):
        make_site(self.site, {"d/" * 70 + "x.js": "1"})
        self.fails(r"too long")

    def test_empty_files_are_refused(self):
        make_site(self.site, {"empty.js": ""})
        self.fails(r"empty")

    @unittest.skipIf(hasattr(os, "geteuid") and os.geteuid() == 0, "root can read any folder")
    def test_an_unreadable_folder_is_an_error_and_not_skipped(self):
        make_site(self.site, {"hidden/secret.js": "x"})
        os.chmod(self.site / "hidden", 0)
        self.addCleanup(os.chmod, self.site / "hidden", 0o755)
        self.fails(r"hidden.*(cannot be read|Permission denied)")

    def test_paths_under_the_api_are_refused(self):
        for name in ("api/v1/users/admin.json", "api/x.json"):
            with self.subTest(name=name):
                shutil.rmtree(self.site)
                make_site(self.site, {name: "{}"})
                self.fails(r"under /api")
        shutil.rmtree(self.site)
        make_site(self.site, {"apiary.js": "1", "apis/x.json": "{}"})
        self.ok()

    def test_failures_are_all_reported_at_once(self):
        make_site(self.site, {".DS_Store": "x", "a.map": "y"})
        result = self.fails(r"hidden")
        self.assertIn("a.map", result.stdout)


PRIVATE_KEY = "-----BEGIN " + "PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASC\n-----END " + "PRIVATE KEY-----\n"


class Secrets(Fixture):
    def test_a_clean_bundle_passes(self):
        self.ok()

    def test_patterns_that_must_not_ship(self):
        cases = {
            "a PEM private key": ("k.txt", PRIVATE_KEY),
            "a PEM certificate": ("c.txt", "-----BEGIN " + "CERTIFICATE-----\nMIIB\n-----END " + "CERTIFICATE-----\n"),
            "a cloud access key": ("a.js", "const key = 'AKIA" + "ABCDEFGHIJKLMNOP';"),
            "a code host token": ("b.js", "const t = 'ghp_" + "a" * 36 + "';"),
            "a chat token": ("c.js", "const t = 'xoxb-" + "1234567890-abcdefghij';"),
            "a signed token": ("d.js", "const t = 'eyJ" + "hbGciOiJIUzI1NiJ9.eyJ" + "zdWIiOiIxIn0.abc-def_ghi';"),
            "a hardware address": ("m.json", '{"mac": "00:1a:2b:3c:4d:5e"}'),
        }
        for label, (name, content) in cases.items():
            with self.subTest(label=label):
                shutil.rmtree(self.site)
                make_site(self.site, {name: content})
                self.fails(re.escape(name) + r".*" + re.escape(label))
                self.assertFalse((self.out / "assets.json").exists())

    def test_ordinary_words_are_not_secrets(self):
        make_site(self.site, {"labels.js": "const text = {password: 'Password', token: 'Session token', "
                                           "key: 'Key', secret: 'Secret'};\n"})
        self.ok()

    def test_a_denied_string_is_found_in_text_and_in_binary_files(self):
        deny = self.root / "deny.txt"
        deny.write_text("# strings that name this device\nSN-0042-ACME\n\n", encoding="utf-8")
        self.ok("--deny-file", str(deny))
        shutil.rmtree(self.out)
        self.out.mkdir()
        make_site(self.site, {"about.js": "const serial = 'SN-0042-ACME';"})
        self.fails(r"about\.js.*SN-0042-ACME", "--deny-file", str(deny))
        make_site(self.site, {"about.js": "ok", "logo.png": PNG + b"SN-0042-ACME"})
        self.fails(r"logo\.png.*SN-0042-ACME", "--deny-file", str(deny))

    def test_the_message_does_not_repeat_the_secret(self):
        make_site(self.site, {"k.txt": PRIVATE_KEY})
        result = self.fails(r"k\.txt")
        self.assertNotIn("MIIEvQIBADANBgkqhkiG9w0BAQEFAASC", result.stdout)

    def test_missing_deny_file_fails(self):
        self.fails(r"deny", "--deny-file", str(self.root / "nowhere.txt"))

    def test_an_empty_deny_entry_is_ignored(self):
        deny = self.root / "deny.txt"
        deny.write_text("\n   \n#only a comment\n", encoding="utf-8")
        self.ok("--deny-file", str(deny))


class Boundaries(unittest.TestCase):
    def test_no_public_header_names_assets(self):
        for header in sorted((REPO / "components/web/include").rglob("*.h")):
            text = re.sub(r"/\*.*?\*/|//[^\n]*", "", header.read_text(encoding="utf-8"), flags=re.DOTALL)
            self.assertNotRegex(text.lower(), r"asset", str(header))

    def test_the_private_header_is_outside_every_public_directory(self):
        private = REPO / "components/web/assets"
        self.assertTrue((private / "web_assets_internal.h").is_file())
        self.assertFalse(list((private).glob("include")), "no include directory: nothing here is public")
        for cmake in (REPO / "components/web/CMakeLists.txt", REPO / "components/web/ports/zephyr/CMakeLists.txt"):
            self.assertNotIn("web/assets", cmake.read_text(encoding="utf-8").replace("\\", "/"), str(cmake))

    def test_nothing_in_the_build_of_the_web_component_includes_the_assets(self):
        for path in (REPO / "components/web/CMakeLists.txt", REPO / "components/web/ports/zephyr/CMakeLists.txt",
                     REPO / "zephyr/CMakeLists.txt"):
            text = path.read_text(encoding="utf-8")
            self.assertNotRegex(text, r"web_assets|crux_web_assets|CruxWebAssets", str(path))


@unittest.skipUnless(shutil.which("cmake"), "needs cmake")
class BuildFunction(Fixture):
    def project(self, body, site=None):
        directory = self.root / "project"
        directory.mkdir(exist_ok=True)
        (directory / "routes.c").write_text('CRUX_WEB_ROUTE(a, "/api/a", CRUX_WEB_GET, "a.read", h, NULL);',
                                            encoding="utf-8")
        (directory / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\nproject(probe C)\n"
            f"include({REPO}/cmake/CruxWebAssets.cmake)\ninclude({REPO}/cmake/CruxWebRoutes.cmake)\n"
            "add_library(crux_web_adapter INTERFACE)\nadd_library(app STATIC routes.c)\n" + body, encoding="utf-8")
        return directory

    def configure(self, directory):
        result = subprocess.run(["cmake", "-S", str(directory), "-B", str(directory / "build")],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        result.stdout = " ".join(result.stdout.split())  # CMake wraps its messages
        return result

    def test_a_missing_folder_fails_to_configure(self):
        directory = self.project(f"crux_web_assets(app FOLDER {self.root}/nowhere RESPONSE_DEADLINE_MS 3000)\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not exist", result.stdout)
        self.assertIn("nowhere", result.stdout)

    def test_a_folder_without_index_fails_to_configure(self):
        (self.root / "empty").mkdir()
        directory = self.project(f"crux_web_assets(app FOLDER {self.root}/empty RESPONSE_DEADLINE_MS 3000)\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("index.html", result.stdout)

    def test_a_folder_argument_is_required(self):
        directory = self.project("crux_web_assets(app RESPONSE_DEADLINE_MS 3000)\n")
        self.assertNotEqual(self.configure(directory).returncode, 0)

    def test_the_style_must_be_one_of_two(self):
        directory = self.project(
            f"crux_web_assets(app FOLDER {self.site} STYLE \"default-src *\" RESPONSE_DEADLINE_MS 3000)\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("STYLE", result.stdout)

    def test_the_deadline_is_needed_from_the_caller_or_kconfig(self):
        directory = self.project(f"crux_web_assets(app FOLDER {self.site})\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RESPONSE_DEADLINE_MS", result.stdout)

    def test_kconfig_deadline_is_read_when_the_caller_gives_none(self):
        directory = self.project(f"set(CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS 3000)\n"
                                 f"crux_web_assets(app FOLDER {self.site})\n")
        self.assertEqual(self.configure(directory).returncode, 0)

    def test_an_explicit_deadline_that_differs_from_kconfig_is_refused(self):
        directory = self.project("set(CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS 3000)\n"
                                 f"crux_web_assets(app FOLDER {self.site} RESPONSE_DEADLINE_MS 10000)\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS", result.stdout)
        self.assertIn("10000", result.stdout)
        self.assertIn("3000", result.stdout)

    def test_an_explicit_deadline_equal_to_kconfig_is_accepted(self):
        directory = self.project("set(CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS 3000)\n"
                                 f"crux_web_assets(app FOLDER {self.site} RESPONSE_DEADLINE_MS 3000)\n")
        self.assertEqual(self.configure(directory).returncode, 0)

    def test_the_bundle_property_set_by_hand_is_not_a_bundle(self):
        (self.root / "hidden.c").write_text(
            'CRUX_WEB_ROUTE(crux_web_asset_route_0000, "/x", CRUX_WEB_GET, CRUX_WEB_PUBLIC, h, NULL);',
            encoding="utf-8")
        directory = self.project(f"set_target_properties(app PROPERTIES CRUX_WEB_ASSETS_SOURCE "
                                 f"{self.root}/hidden.c)\ncrux_web_routes(app SOURCES routes.c)\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("crux_web_assets", result.stdout)

    def test_the_kconfig_deadline_is_what_the_packager_uses(self):
        directory = self.project("set(CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS 1500)\n"
                                 f"crux_web_assets(app FOLDER {self.site})\n")
        self.assertEqual(self.configure(directory).returncode, 0)
        built = subprocess.run(["cmake", "--build", str(directory / "build"), "--target", "crux_web_assets_app"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        self.assertEqual(built.returncode, 0, built.stdout)
        budget = json.loads(next((directory / "build").rglob("crux-web-assets.json")).read_text(
            encoding="utf-8"))["budget"]
        self.assertEqual(budget["response_deadline_ms"], 1500)
        self.assertEqual(budget["max_asset_bytes"], 49152)
        self.assertIn("1500 ms", built.stdout)

    def test_assets_must_come_before_the_routes(self):
        directory = self.project(f"crux_web_routes(app SOURCES routes.c)\n"
                                 f"crux_web_assets(app FOLDER {self.site} RESPONSE_DEADLINE_MS 3000)\n")
        result = self.configure(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("before crux_web_routes", result.stdout)

    def test_a_good_folder_configures_and_the_build_runs_the_packager(self):
        directory = self.project(f"crux_web_assets(app FOLDER {self.site} RESPONSE_DEADLINE_MS 3000)\n"
                                 "crux_web_routes(app SOURCES routes.c REVIEWED reviewed.txt)\n")
        (directory / "reviewed.txt").write_text("public GET @assets\n", encoding="utf-8")
        self.assertEqual(self.configure(directory).returncode, 0)
        built = subprocess.run(["cmake", "--build", str(directory / "build"), "--target", "crux_web_assets_app"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        self.assertEqual(built.returncode, 0, built.stdout)
        listing = next((directory / "build").rglob("crux-web-assets.json"))
        self.assertEqual(json.loads(listing.read_text(encoding="utf-8"))["totals"]["files"], 4)

    def test_the_build_fails_when_the_budget_is_exceeded(self):
        directory = self.project(f"crux_web_assets(app FOLDER {self.site} RESPONSE_DEADLINE_MS 3000 "
                                 "MAX_FILES 2)\n")
        self.assertEqual(self.configure(directory).returncode, 0)
        built = subprocess.run(["cmake", "--build", str(directory / "build"), "--target", "crux_web_assets_app"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        self.assertNotEqual(built.returncode, 0)
        self.assertIn("at most 2", built.stdout)

    def test_a_build_without_assets_has_none_of_it(self):
        directory = self.project("crux_web_routes(app SOURCES routes.c)\n")
        self.assertEqual(self.configure(directory).returncode, 0)
        cache = (directory / "build/CMakeCache.txt").read_text(encoding="utf-8")
        self.assertNotIn("crux_web_assets", cache)
        self.assertEqual(list((directory / "build").rglob("crux_web_assets*")), [])


if __name__ == "__main__":
    unittest.main()
