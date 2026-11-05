"""The web route generator and the review list, without a compiler or Zephyr."""
import importlib.util
from pathlib import Path
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

    def test_repository_list_holds(self):
        result = subprocess.run([sys.executable, str(CHECK)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
