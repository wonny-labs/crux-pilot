#!/usr/bin/env python3
"""The small example project of Issue #220 needs no native header and nothing outside the public contracts.

samples/web_project adds a page and an API route. Its C sources may include Crux's public web header and
the C standard library, and nothing from Zephyr, a board, a vendor or a component's private folders. Its page
must load under the strict content policy, so it holds no inline script, inline style or other origin. The
Host suite Web.project_sample serves it on the real core.
"""
import pathlib
import re
import unittest

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
SAMPLE = REPOSITORY / "samples/web_project"
STANDARD = {"stdbool.h", "stddef.h", "stdint.h", "string.h", "stdio.h", "stdlib.h"}
PUBLIC = {"crux/web.h"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def includes(text):
    return INCLUDE.findall(text)


def native_errors(name, text):
    """Each include of a source that is neither public Crux nor the C standard library."""
    return [f"{name}: includes {header}" for header in includes(text) if header not in STANDARD | PUBLIC]


def page_errors(name, text):
    errors = []
    for match in re.finditer(r"<script\b([^>]*)>", text, re.IGNORECASE):
        if "src=" not in match.group(1):
            errors.append(f"{name}: an inline script")
    if re.search(r"\sstyle\s*=", text, re.IGNORECASE) or re.search(r"<style\b", text, re.IGNORECASE):
        errors.append(f"{name}: an inline style")
    if re.search(r"""(?:src|href)\s*=\s*["']?(?:https?:)?//""", text, re.IGNORECASE):
        errors.append(f"{name}: a resource from another origin")
    return errors


class ProjectSample(unittest.TestCase):
    def test_the_sources_include_no_native_header(self):
        sources = sorted((SAMPLE / "src").glob("*.c"))
        self.assertTrue(sources)
        for source in sources:
            with self.subTest(source=source.name):
                self.assertEqual(native_errors(source.name, source.read_text()), [])
                self.assertIn("crux/web.h", includes(source.read_text()))

    def test_the_check_refuses_a_native_header(self):
        for header in ("zephyr/kernel.h", "zephyr/net/http/service.h", "crux_web_adapter.h", "fsl_gpio.h",
                       "web_assets_internal.h"):
            with self.subTest(header=header):
                self.assertTrue(native_errors("x.c", f"#include <{header}>\n"))

    def test_the_route_is_reviewed_and_registered_through_the_contract(self):
        route = (SAMPLE / "src/greeting_route.c").read_text()
        self.assertRegex(route, r'CRUX_WEB_ROUTE\(project_greeting, "/api/v1/project/greeting", CRUX_WEB_GET, '
                                r'CRUX_WEB_SESSION')
        reviewed = (SAMPLE / "reviewed-routes.txt").read_text().splitlines()
        self.assertIn("session GET /api/v1/project/greeting", reviewed)
        self.assertIn("public GET @assets", reviewed)

    def test_the_page_fits_the_strict_policy(self):
        for page in sorted((SAMPLE / "site").glob("*.html")):
            with self.subTest(page=page.name):
                self.assertEqual(page_errors(page.name, page.read_text()), [])

    def test_the_page_check_refuses_inline_content(self):
        for text in ("<script>alert(1)</script>", '<p style="color:red">', "<style>p{}</style>",
                     '<script src="https://cdn.example/x.js"></script>'):
            with self.subTest(text=text):
                self.assertTrue(page_errors("x.html", text))


if __name__ == "__main__":
    unittest.main()
