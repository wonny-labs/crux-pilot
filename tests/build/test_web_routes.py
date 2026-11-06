"""The web route generator and the review list, without a compiler or Zephyr."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("web_routes_under_test",
                                              REPO / "components/web/tools/web_routes.py")
web_routes = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = web_routes
SPEC.loader.exec_module(web_routes)
CHECK = REPO / "scripts/check-web-routes.py"


def routes_of(text):
    return web_routes.parse_source(text, "fixture.c")


def resources_of(text):
    return web_routes.build_resources(routes_of(text))


class Parsing(unittest.TestCase):
    def refuses(self, text, reason):
        with self.assertRaisesRegex(web_routes.RouteError, reason):
            resources_of(text)

    def test_reads_every_access_form(self):
        routes = routes_of('''
            CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, "app.read", on_a, NULL);
            CRUX_WEB_ROUTE(b, "/b/{id}", CRUX_WEB_GET | CRUX_WEB_PUT, CRUX_WEB_SESSION, on_b, &ctx);
            CRUX_WEB_ROUTE_BODY(c, "/c", CRUX_WEB_POST, CRUX_WEB_PUBLIC, on_c, NULL, 256u);
        ''')
        self.assertEqual([route.access_class for route in routes], ["permission", "session", "public"])
        self.assertEqual(routes[1].pattern, "/b/*")
        self.assertEqual(routes[1].methods, ("CRUX_WEB_GET", "CRUX_WEB_PUT"))

    def test_ignores_comments_and_strings(self):
        self.assertEqual(routes_of('''
            /* CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, "x", on_a, NULL); */
            // CRUX_WEB_ROUTE(b, "/b", CRUX_WEB_GET, "x", on_b, NULL);
            const char *text = "CRUX_WEB_ROUTE(c";
        '''), [])

    def test_missing_access_class(self):
        self.refuses('CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, on_a, NULL);', "mandatory access class")

    def test_bad_access_class(self):
        for access in ("NULL", "0", "permission", '""', '"a b"', '"@session"', '"a" "b"'):
            self.refuses(f'CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, {access}, on_a, NULL);',
                         "needs an access class")

    def test_path_must_be_a_plain_literal(self):
        self.refuses('CRUX_WEB_ROUTE(a, PATH, CRUX_WEB_GET, "x", on_a, NULL);', "one string literal")
        for path in ("a", "/a/", "/a//b", "/a/*", "/a?b", "/a/{id}x", "/a/{Id}", "/a/[b]", "/a/../b",
                     "/{a}/{b}/{c}/{d}/{e}"):
            with self.subTest(path=path):
                with self.assertRaises(web_routes.RouteError):
                    resources_of(f'CRUX_WEB_ROUTE(a, "{path}", CRUX_WEB_GET, "x", on_a, NULL);')

    def test_methods_must_be_plain_bits(self):
        for methods in ("1", "METHODS", "CRUX_WEB_GET|CRUX_WEB_GET", "CRUX_WEB_HEAD"):
            self.refuses(f'CRUX_WEB_ROUTE(a, "/a", {methods}, "x", on_a, NULL);', "method")

    def test_macro_defining_routes_refused(self):
        self.refuses('#define MINE(n) CRUX_WEB_ROUTE(n, "/a", CRUX_WEB_GET, "x", on_a, NULL)\n',
                     "cannot be read")

    def test_redefining_a_web_macro_refused(self):
        for line in ("#define CRUX_WEB_PUBLIC \"x.y\"", "#undef CRUX_WEB_GET", "  #  define CRUX_WEB_ROUTE(x) x",
                     "#define CRUX_WEB_BODY_BUFFER 9"):
            with self.subTest(line=line):
                self.refuses(line + '\nCRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, "x", on_a, NULL);', "may not")

    def test_continued_directives_are_read_too(self):
        for text in ("#define \\\n    CRUX_WEB_PUBLIC \"x.y\"\n",
                     "#undef \\\nCRUX_WEB_GET\n",
                     "# \\\n  define CRUX_WEB_BODY_BUFFER 9\n"):
            with self.subTest(text=text):
                self.refuses(text + 'CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, "x", on_a, NULL);', "may not")
        self.refuses('#define MINE(n) \\\n  CRUX_WEB_ROUTE(n, "/a", CRUX_WEB_GET, "x", on_a, NULL)\n',
                     "cannot be read")
        # The line number still points at the use.
        with self.assertRaisesRegex(web_routes.RouteError, r"fixture.c:3:"):
            routes_of('int a = 1 + \\\n    2;\nCRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, on_a, NULL);')

    def test_table_holds_what_the_scan_read(self):
        text = web_routes.render_table(routes_of(
            'CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET | CRUX_WEB_PUT, "x.y", h, NULL);'
            'CRUX_WEB_ROUTE(b, "/b", CRUX_WEB_POST, CRUX_WEB_PUBLIC, h, NULL);'))
        self.assertIn('{"/a", CRUX_WEB_GET | CRUX_WEB_PUT, "x.y", NULL, NULL, 0u},', text)
        self.assertIn('{"/b", CRUX_WEB_POST, CRUX_WEB_PUBLIC, NULL, NULL, 0u},', text)

    def test_duplicates_refused(self):
        self.refuses('''CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, "x", on_a, NULL);
                        CRUX_WEB_ROUTE(a, "/b", CRUX_WEB_GET, "x", on_a, NULL);''', "route name")
        self.refuses('''CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_GET, "x", on_a, NULL);
                        CRUX_WEB_ROUTE(b, "/a", CRUX_WEB_GET, "y", on_a, NULL);''', "repeats")
        self.refuses('''CRUX_WEB_ROUTE(a, "/a/{x}", CRUX_WEB_GET, "x", on_a, NULL);
                        CRUX_WEB_ROUTE(b, "/a/{y}", CRUX_WEB_GET, "y", on_a, NULL);''', "repeats")

    def test_overlapping_wildcards_refused(self):
        self.refuses('''CRUX_WEB_ROUTE(a, "/a/{x}/c", CRUX_WEB_GET, "x", on_a, NULL);
                        CRUX_WEB_ROUTE(b, "/a/b/{y}", CRUX_WEB_GET, "y", on_a, NULL);''', "same request")


class Resources(unittest.TestCase):
    TABLE = '''
        CRUX_WEB_ROUTE(login, "/api/v1/session", CRUX_WEB_POST, CRUX_WEB_PUBLIC, h, NULL);
        CRUX_WEB_ROUTE(logout, "/api/v1/session", CRUX_WEB_DELETE, CRUX_WEB_SESSION, h, NULL);
        CRUX_WEB_ROUTE(attempt, "/api/v1/session/attempt/{id}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, h, NULL);
        CRUX_WEB_ROUTE(me, "/api/v1/session/me", CRUX_WEB_GET, CRUX_WEB_SESSION, h, NULL);
        CRUX_WEB_ROUTE(list, "/api/v1/files/{area}", CRUX_WEB_GET, CRUX_WEB_SESSION, h, NULL);
        CRUX_WEB_ROUTE(item, "/api/v1/files/{area}/{name}", CRUX_WEB_GET, CRUX_WEB_SESSION, h, NULL);
    '''

    def test_one_resource_per_path_with_methods_merged(self):
        resources = resources_of(self.TABLE)
        self.assertEqual(len({resource.pattern for resource in resources}), len(resources))
        session = next(item for item in resources if item.pattern == "/api/v1/session")
        self.assertEqual(session.sorted_methods(), ["CRUX_WEB_POST", "CRUX_WEB_DELETE"])

    def test_section_order(self):
        self.assertEqual([resource.pattern for resource in resources_of(self.TABLE)], [
            "/api/v1/files/*/*",
            "/api/v1/session/attempt/*",
            "/api/v1/files/*",
            "/api/v1/session/me",
            "/api/v1/session",
        ])

    def test_generated_zephyr_names_sort_in_that_order(self):
        text = web_routes.render_zephyr(resources_of(self.TABLE))
        self.assertIn('CRUX_WEB_ZEPHYR_RESOURCE(0000, "/api/v1/files/*/*", BIT(HTTP_GET));', text)
        self.assertIn('CRUX_WEB_ZEPHYR_RESOURCE(0004, "/api/v1/session", BIT(HTTP_POST) | BIT(HTTP_DELETE));',
                      text)

    def test_table_lists_fixed_routes_first(self):
        text = web_routes.render_table(routes_of(self.TABLE))
        self.assertLess(text.index("&crux_web_route_me"), text.index("&crux_web_route_list"))
        self.assertIn("crux_web_route_count = 6;", text)

    def test_no_routes_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "none.c"
            source.write_text("int nothing;\n", encoding="utf-8")
            self.assertEqual(web_routes.main([str(source), "--table", f"{directory}/t.c"]), 1)


class BuildFunction(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cmake"), "needs cmake")
    def test_generated_resources_need_a_reviewed_list(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "routes.c").write_text("", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(probe NONE)\n"
                f"include({REPO}/cmake/CruxWebRoutes.cmake)\n"
                "add_library(app INTERFACE)\n"
                "crux_web_routes(app SOURCES routes.c ZEPHYR_RESOURCES)\n", encoding="utf-8")
            result = subprocess.run(["cmake", "-S", directory, "-B", f"{directory}/build"],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("REVIEWED", result.stdout)


class ReviewList(unittest.TestCase):
    def run_check(self, sources, reviewed):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "routes.c"
            source.write_text(sources, encoding="utf-8")
            listing = Path(directory) / "reviewed.txt"
            listing.write_text(reviewed, encoding="utf-8")
            return subprocess.run([sys.executable, str(CHECK), "--reviewed", str(listing), str(source)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)

    SOURCE = '''CRUX_WEB_ROUTE(a, "/a", CRUX_WEB_POST, CRUX_WEB_PUBLIC, h, NULL);
                CRUX_WEB_ROUTE(b, "/b", CRUX_WEB_GET, CRUX_WEB_SESSION, h, NULL);
                CRUX_WEB_ROUTE(c, "/c", CRUX_WEB_GET, "c.read", h, NULL);'''

    def test_lists_and_accepts(self):
        result = self.run_check(self.SOURCE, "# reviewed\npublic POST /a\nsession GET /b\n")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("public", result.stdout)
        self.assertIn("c.read", result.stdout)

    def test_unreviewed_route_fails(self):
        result = self.run_check(self.SOURCE, "public POST /a\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not reviewed: session GET /b", result.stdout)

    def test_stale_entry_fails(self):
        result = self.run_check(self.SOURCE, "public POST /a\nsession GET /b\npublic GET /gone\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no such route: public GET /gone", result.stdout)

    def test_a_route_changing_class_fails(self):
        result = self.run_check(self.SOURCE, "session POST /a\nsession GET /b\n")
        self.assertEqual(result.returncode, 1)

    def test_generator_holds_routes_to_the_list_at_build(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "generated.inc"
            source.write_text(self.SOURCE, encoding="utf-8")
            listing = Path(directory) / "reviewed.txt"
            listing.write_text("public POST /a\n", encoding="utf-8")
            table = f"{directory}/table.c"
            self.assertEqual(web_routes.main([str(source), "--table", table, "--reviewed", str(listing)]), 1)
            self.assertFalse(Path(table).exists())
            listing.write_text("public POST /a\nsession GET /b\n", encoding="utf-8")
            self.assertEqual(web_routes.main([str(source), "--table", table, "--reviewed", str(listing)]), 0)

    def test_default_scan_reads_every_kind_of_source(self):
        check = importlib.util.spec_from_file_location("check_web_routes", CHECK)
        module = importlib.util.module_from_spec(check)
        check.loader.exec_module(module)
        for suffix in (".c", ".cc", ".cpp", ".inc", ".def", ".h"):
            self.assertIn(suffix, module.SOURCE_SUFFIXES)

    def load_check(self):
        check = importlib.util.spec_from_file_location("check_web_routes", CHECK)
        module = importlib.util.module_from_spec(check)
        check.loader.exec_module(module)
        return module

    def sample(self, root, listing=None, cmake=None):
        """A sample under root/samples/rogue with one public route, and optionally its list and a build file."""
        source = root / "samples/rogue/src/r.c"
        source.parent.mkdir(parents=True)
        source.write_text('#include <crux/web.h>\nCRUX_WEB_ROUTE(rogue_route, "/api/v1/rogue", CRUX_WEB_POST, '
                          'CRUX_WEB_PUBLIC, h, NULL);\n', encoding="utf-8")
        if listing is not None:
            (root / "samples/rogue/reviewed-routes.txt").write_text(listing, encoding="utf-8")
        if cmake is not None:
            (root / "tests/rogue").mkdir(parents=True)
            (root / "tests/rogue/CMakeLists.txt").write_text(cmake, encoding="utf-8")
        return source

    def sample_errors(self, root):
        module = self.load_check()
        source = root / "samples/rogue/src/r.c"
        listing = module.own_list(source, root)
        if listing is None:
            return ["no own list"]
        return module.sample_errors({listing: [source]}, root)

    NAMED = "crux_web_routes(app SOURCES r.c REVIEWED ${CMAKE_CURRENT_LIST_DIR}/../../samples/rogue/reviewed-routes.txt)\n"

    def test_an_empty_list_of_a_sample_does_not_exempt_its_route(self):
        # The review's repro: an empty list beside a public route, which the old exemption skipped.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.sample(root, listing="", cmake=self.NAMED)
            self.assertIn("not reviewed: public POST /api/v1/rogue", " ".join(self.sample_errors(root)))

    def test_a_list_that_no_build_names_exempts_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.sample(root, listing="public POST /api/v1/rogue\n")
            errors = self.sample_errors(root)
            self.assertTrue(any("no build names it" in error for error in errors), errors)
            # A build file that names the list without REVIEWED does not count either.
            (root / "tests/rogue").mkdir(parents=True)
            (root / "tests/rogue/CMakeLists.txt").write_text(
                "# samples/rogue/reviewed-routes.txt\n", encoding="utf-8")
            self.assertTrue(any("no build names it" in error for error in self.sample_errors(root)))

    def test_a_named_list_that_holds_the_routes_passes_and_a_stale_entry_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.sample(root, listing="public POST /api/v1/rogue\npublic GET @assets\n", cmake=self.NAMED)
            # The packaged folder's line is held by the build, which sees the generated source; the scan cannot.
            self.assertEqual(self.sample_errors(root), [])
            (root / "samples/rogue/reviewed-routes.txt").write_text(
                "public POST /api/v1/rogue\nsession GET /api/v1/gone\n", encoding="utf-8")
            self.assertIn("reviewed, but no such route: session GET /api/v1/gone",
                          " ".join(self.sample_errors(root)))

    def test_without_a_list_a_sample_falls_to_the_default_list(self):
        module = self.load_check()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = self.sample(root)
            self.assertIsNone(module.own_list(source, root))

    def test_the_example_project_is_checked_against_its_named_list(self):
        module = self.load_check()
        sample = REPO / "samples/web_project"
        listing = module.own_list(sample / "src/greeting_route.c")
        self.assertEqual(listing, sample / "reviewed-routes.txt")
        self.assertTrue(module.named_by_build(listing))
        self.assertEqual(module.sample_errors({listing: [sample / "src/greeting_route.c"]}), [])
        self.assertIsNone(module.own_list(REPO / "components/web/session/web_session_routes.c"))
        self.assertIsNone(module.own_list(REPO / "samples/time/src/time_command.c"))
        default, owned = module.default_sources()
        self.assertNotIn(sample / "src/greeting_route.c", default)
        self.assertEqual(owned, {listing: [sample / "src/greeting_route.c"]})

    def test_repository_list_holds(self):
        result = subprocess.run([sys.executable, str(CHECK)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
