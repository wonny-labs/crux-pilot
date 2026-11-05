#!/usr/bin/env python3
"""List the public and session-only web routes, and hold them to a reviewed list.

CRUX_WEB_PUBLIC is reserved for login, its attempt result and static sign-in assets, and a session-only
route must check permissions in its handler or service. Neither can be told from the code alone, so a
person reviews this list. With --reviewed, a route of those classes that is not on the list fails, and
so does an entry with no route, so the list changes with the code and shows up in a review.

    scripts/check-web-routes.py [--reviewed FILE] [SOURCE ...]

With no source it reads every C source under components/ and samples/ that uses the macro. A line of
the reviewed list reads `public POST /api/v1/session`, and # starts a comment.
"""
import argparse
import importlib.util
from pathlib import Path
import sys

REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("web_routes",
                                              REPO / "components/web/tools/web_routes.py")
web_routes = importlib.util.module_from_spec(SPEC)
sys.modules["web_routes"] = web_routes
SPEC.loader.exec_module(web_routes)
METHOD_NAME = {token: token.removeprefix("CRUX_WEB_") for token in web_routes.METHODS}
DEFAULT_LIST = REPO / "components/web/reviewed-routes.txt"


def default_sources():
    found = []
    for root in (REPO / "components", REPO / "samples"):
        for path in sorted(root.rglob("*.[ch]")) + sorted(root.rglob("*.cpp")):
            if path == REPO / "components/web/include/crux/web.h":
                continue
            if web_routes.MACRO.search(web_routes.strip_comments(path.read_text(encoding="utf-8"))):
                found.append(path)
    return found


def lines_for(routes):
    """One sorted `class METHOD path` line for each method of each route that needs a review."""
    lines = []
    for route in routes:
        if route.access_class in ("public", "session"):
            for method in route.methods:
                lines.append(f"{route.access_class} {METHOD_NAME[method]} {route.path}")
    return sorted(lines)


def read_reviewed(path):
    entries = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.append(" ".join(line.split()))
    return sorted(entries)


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
    found = lines_for(routes)
    for route in sorted(routes, key=lambda route: (route.access_class, route.path)):
        methods = "|".join(METHOD_NAME[method] for method in route.methods)
        access = route.access if route.access_class == "permission" else route.access_class
        print(f"{route.access_class:<10} {methods:<16} {route.path:<40} {access}")
    print(f"{len(found)} public or session-only method routes in {len(routes)} routes")
    if reviewed is None:
        return 0
    expected = read_reviewed(reviewed)
    failed = False
    for entry in sorted(set(found) - set(expected)):
        print(f"not reviewed: {entry}", file=sys.stderr)
        failed = True
    for entry in sorted(set(expected) - set(found)):
        print(f"reviewed, but no such route: {entry}", file=sys.stderr)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
