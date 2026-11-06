#!/usr/bin/env python3
"""List the public and session-only web routes, and hold them to a reviewed list.

CRUX_WEB_PUBLIC is reserved for login, its attempt result and static sign-in assets, and a session-only
route must check permissions in its handler or service. Neither can be told from the code alone, so a
person reviews this list. With --reviewed, a route of those classes that is not on the list fails, and
so does an entry with no route, so the list changes with the code and shows up in a review.

    scripts/check-web-routes.py [--reviewed FILE] [SOURCE ...]

With no source it reads every tracked source outside tests/ that uses the macro, which cannot see a
generated source: for those, name the list at build, with crux_web_routes(... REVIEWED file). A sample
under samples/ that keeps its own reviewed-routes.txt beside its sources, as a product would, is left to
that list, which its build names (Issue #220); it is listed here as skipped. A line of
the reviewed list reads `public POST /api/v1/session`, and # starts a comment.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("web_routes",
                                              REPO / "components/web/tools/web_routes.py")
web_routes = importlib.util.module_from_spec(SPEC)
sys.modules["web_routes"] = web_routes
SPEC.loader.exec_module(web_routes)
METHOD_NAME = {token: token.removeprefix("CRUX_WEB_") for token in web_routes.METHODS}
DEFAULT_LIST = REPO / "components/web/reviewed-routes.txt"
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".inc", ".def"}


def own_list(path):
    """The reviewed list a sample keeps beside its sources, or None."""
    samples = REPO / "samples"
    for parent in path.parents:
        if parent == samples or samples not in parent.parents:
            return None
        if (parent / "reviewed-routes.txt").is_file():
            return parent / "reviewed-routes.txt"
    return None


def default_sources():
    tracked = subprocess.run(["git", "-C", str(REPO), "ls-files", "--cached", "--others",
                              "--exclude-standard"], stdout=subprocess.PIPE, text=True, check=True)
    found = []
    for name in tracked.stdout.splitlines():
        path = REPO / name
        if (path.suffix not in SOURCE_SUFFIXES or name.startswith("tests/") or not path.is_file() or
                path == REPO / "components/web/include/crux/web.h"):
            continue
        if web_routes.MACRO.search(web_routes.strip_comments(path.read_text(encoding="utf-8"))):
            if own_list(path) is not None:
                print(f"skipped: {name}, held to {own_list(path).relative_to(REPO)} by its build")
                continue
            found.append(path)
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sources", nargs="*", type=Path)
    parser.add_argument("--reviewed", type=Path, help=f"Default with no source: {DEFAULT_LIST.name}")
    args = parser.parse_args(argv)
    sources = args.sources or default_sources()
    reviewed = args.reviewed or (None if args.sources else DEFAULT_LIST)
    try:
        routes = web_routes.parse_sources(sources)
        if routes:
            web_routes.build_resources(routes)
    except (web_routes.RouteError, OSError) as error:
        print(f"check-web-routes: {error}", file=sys.stderr)
        return 1
    found = web_routes.review_lines(routes)
    for route in sorted(routes, key=lambda route: (route.access_class, route.path)):
        methods = "|".join(METHOD_NAME[method] for method in route.methods)
        access = route.access if route.access_class == "permission" else route.access_class
        print(f"{route.access_class:<10} {methods:<16} {route.path:<40} {access}")
    print(f"{len(found)} public or session-only method routes in {len(routes)} routes")
    if reviewed is None:
        return 0
    errors = web_routes.review_errors(routes, reviewed)
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
