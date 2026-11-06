#!/usr/bin/env python3
"""List the public and session-only web routes, and hold them to a reviewed list.

CRUX_WEB_PUBLIC is reserved for login, its attempt result and static sign-in assets, and a session-only
route must check permissions in its handler or service. Neither can be told from the code alone, so a
person reviews this list. With --reviewed, a route of those classes that is not on the list fails, and
so does an entry with no route, so the list changes with the code and shows up in a review.

    scripts/check-web-routes.py [--reviewed FILE] [SOURCE ...]

With no source it reads every tracked source outside tests/ that uses the macro, which cannot see a
generated source: for those, name the list at build, with crux_web_routes(... REVIEWED file). A sample
under samples/ that keeps its own reviewed-routes.txt beside its sources, as a product would (Issue #220),
is checked against that list in place of the default one, by the same comparison, and that list must be
named after REVIEWED in a tracked CMake file, so that a list no build uses exempts nothing. The list's
`public GET @assets` line is left to the build, which sees the packaged folder's generated source. A line of
the reviewed list reads `public POST /api/v1/session`, and # starts a comment.
"""
import argparse
import importlib.util
import re
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


def own_list(path, root=REPO):
    """The reviewed list a sample keeps beside its sources, or None."""
    samples = root / "samples"
    for parent in path.parents:
        if parent == samples or samples not in parent.parents:
            return None
        if (parent / "reviewed-routes.txt").is_file():
            return parent / "reviewed-routes.txt"
    return None


def named_by_build(listing, root=REPO):
    """Whether a tracked CMake file, or under a test's root any CMake file, names the list after REVIEWED."""
    relative = listing.relative_to(root).as_posix()
    pattern = re.compile(r"\bREVIEWED\s+\S*(?:^|/)" + re.escape(relative) + r"(?=[\s)])", re.MULTILINE)
    if root == REPO:
        names = subprocess.run(["git", "-C", str(root), "ls-files", "--", "*CMakeLists.txt", "*.cmake"],
                               stdout=subprocess.PIPE, text=True, check=True).stdout.splitlines()
        files = [root / name for name in names]
    else:
        files = [*root.rglob("CMakeLists.txt"), *root.rglob("*.cmake")]
    return any(path.is_file() and pattern.search(path.read_text(encoding="utf-8")) for path in files)


def sample_errors(owned, root=REPO):
    """Each sample's routes against its own list, which a build must name."""
    errors = []
    for listing, sources in sorted(owned.items()):
        name = listing.relative_to(root).as_posix()
        if not named_by_build(listing, root):
            errors.append(f"{name}: no build names it after REVIEWED, so it cannot stand for the default list")
        try:
            routes = web_routes.parse_sources(sources)
        except (web_routes.RouteError, OSError) as error:
            errors.append(f"{name}: {error}")
            continue
        found = set(web_routes.review_lines(routes))
        expected = set(web_routes.read_reviewed(listing)) - {web_routes.BUNDLE_REVIEW}
        errors += [f"{name}: not reviewed: {entry}" for entry in sorted(found - expected)]
        errors += [f"{name}: reviewed, but no such route: {entry}" for entry in sorted(expected - found)]
    return errors


def default_sources():
    tracked = subprocess.run(["git", "-C", str(REPO), "ls-files", "--cached", "--others",
                              "--exclude-standard"], stdout=subprocess.PIPE, text=True, check=True)
    found, owned = [], {}
    for name in tracked.stdout.splitlines():
        path = REPO / name
        if (path.suffix not in SOURCE_SUFFIXES or name.startswith("tests/") or not path.is_file() or
                path == REPO / "components/web/include/crux/web.h"):
            continue
        if web_routes.MACRO.search(web_routes.strip_comments(path.read_text(encoding="utf-8"))):
            listing = own_list(path)
            if listing is not None:
                owned.setdefault(listing, []).append(path)
                continue
            found.append(path)
    return found, owned


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sources", nargs="*", type=Path)
    parser.add_argument("--reviewed", type=Path, help=f"Default with no source: {DEFAULT_LIST.name}")
    args = parser.parse_args(argv)
    owned = {}
    sources = args.sources
    if not sources:
        sources, owned = default_sources()
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
    errors = web_routes.review_errors(routes, reviewed) + sample_errors(owned)
    for listing, owned_sources in sorted(owned.items()):
        print(f"checked against {listing.relative_to(REPO)}: {len(owned_sources)} source(s)")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
