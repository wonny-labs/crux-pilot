#!/usr/bin/env python3
"""Build-time route table for crux_web.

Reads the CRUX_WEB_ROUTE() uses in the named source files and writes:

- the route table the portable core looks routes up in, and
- one registered resource per path, with its methods merged, in the order the Zephyr server needs.

The server's order rules come from its source at v4.4.2. Its exact pass runs first. Its wildcard pass
then takes the first match in section order, sorted by name, with FNM_LEADING_DIR, so a resource also
matches every deeper path. Generated names are therefore numbered: wildcard resources first, deeper
before shallower, then fixed resources, deeper before shallower. A duplicate resource for a path would
answer 405 for the second method, so a path gets one resource.

It accepts only what it can read for certain, and it stops on anything else. The compiler checks the
same macro use, so a route the scan missed is unreachable rather than unprotected, and a route the
scan saw but the compiler did not fails to link.
"""
import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys

ACCESS_SESSION = "CRUX_WEB_SESSION"
ACCESS_PUBLIC = "CRUX_WEB_PUBLIC"
METHODS = {
    "CRUX_WEB_GET": "HTTP_GET",
    "CRUX_WEB_POST": "HTTP_POST",
    "CRUX_WEB_PUT": "HTTP_PUT",
    "CRUX_WEB_DELETE": "HTTP_DELETE",
    "CRUX_WEB_PATCH": "HTTP_PATCH",
}
METHOD_ORDER = list(METHODS)
PERMISSION = re.compile(r"[A-Za-z][A-Za-z0-9_.-]{0,62}\Z")
ROUTE_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
FIXED_SEGMENT = re.compile(r"[A-Za-z0-9._~-]+\Z")
PARAM_SEGMENT = re.compile(r"\{[a-z][a-z0-9_]*\}\Z")
MACRO = re.compile(r"\bCRUX_WEB_ROUTE(_BODY)?\s*\(")
REDEFINED = re.compile(r"^[ \t]*#[ \t]*(define|undef)[ \t]+(CRUX_WEB_\w*)", re.MULTILINE)
CONTINUATION = re.compile(r"\\\r?\n")
PATH_PARAMS_MAX = 4
# The routes of a packaged web folder (#189) are public and GET, and their set changes with every
# build of the frontend, so the reviewed list holds one entry for all of them. The packager's checks
# and its hash list are what a person reviews for the content.
BUNDLE_ROUTE_PREFIX = "crux_web_asset_route_"
BUNDLE_REVIEW = "public GET @assets"
BUNDLE_HANDLER = "crux_web_assets_serve"
BUNDLE_CONTEXT = re.compile(r"&crux_web_asset_table\[\d+\]\Z")
# What web_assets.py writes, and the only forms that a bundle source may hold: two preprocessor lines,
# then data, header lists, the table, its count and the routes. Anything else, such as a #define that
# renames the handler, an #include, or another statement, is refused, because the scan reads names
# as text and the preprocessor could change what they mean.
_STRING = r'"[^"\\\n]*"'
BUNDLE_PREAMBLE = re.compile(r'\s*#define CRUX_ASSETS_INLINE_STYLES [01]\n#include "web_assets_internal\.h"\n')
BUNDLE_STATEMENTS = [re.compile(pattern) for pattern in (
    r"static const uint8_t crux_web_asset_data_\d{4}\[\] = \{[\s0-9a-fx,]*\};",
    r"static const crux_web_header_t crux_web_asset_headers_\d{4}\[\] = \{(?:\s*(?:\{" + _STRING + ", " +
    _STRING + r"\}|CRUX_WEB_ASSETS_SECURITY_HEADERS),)*\s*\};",
    r"const crux_web_asset_t crux_web_asset_table\[\] = \{(?:\s*\{" + _STRING +
    r", crux_web_asset_data_\d{4}, sizeof\(crux_web_asset_data_\d{4}\), crux_web_asset_headers_\d{4}, "
    r"\d+u\},)*\s*\};",
    r"const size_t crux_web_asset_count = \d+u;",
    r"CRUX_WEB_ROUTE\((?:[^;\"]|" + _STRING + r")*\);",
)]


class RouteError(Exception):
    """A route the generator refuses, with where it is."""


@dataclass(frozen=True)
class Route:
    name: str
    path: str
    methods: tuple
    access: str  # A permission name, or ACCESS_SESSION or ACCESS_PUBLIC.
    source: str
    line: int
    bundle: bool = False  # From a packaged web folder: see BUNDLE_REVIEW and parse_source.

    @property
    def pattern(self):
        return "/".join("*" if part.startswith("{") else part for part in self.path.split("/"))

    @property
    def wildcard(self):
        return "*" in self.pattern

    @property
    def access_class(self):
        if self.access == ACCESS_PUBLIC:
            return "public"
        if self.access == ACCESS_SESSION:
            return "session"
        return "permission"


@dataclass
class Resource:
    pattern: str
    methods: set = field(default_factory=set)

    @property
    def wildcard(self):
        return "*" in self.pattern

    @property
    def depth(self):
        return self.pattern.count("/")

    def sorted_methods(self):
        return [method for method in METHOD_ORDER if method in self.methods]


def strip_comments(text):
    """Blank out comments, keeping line numbers and string literals."""

    def blank(match):
        token = match.group(0)
        if token.startswith('"') or token.startswith("'"):
            return token
        return re.sub(r"[^\n]", " ", token)

    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', blank, text,
                  flags=re.DOTALL)


def blank_strings(text):
    """The same text with the inside of each string literal blanked, for finding macro uses."""
    return re.sub(r'"(?:\\.|[^"\\])*"', lambda match: '"' + " " * (len(match.group(0)) - 2) + '"', text)


def split_arguments(text, start, where):
    """Split the arguments of a macro use whose opening parenthesis is just before start."""
    arguments, depth, current, index = [], 0, [], start
    while index < len(text):
        character = text[index]
        if character == '"':
            literal = re.compile(r'"(?:\\.|[^"\\])*"').match(text, index)
            if not literal:
                raise RouteError(f"{where}: unterminated string")
            current.append(literal.group(0))
            index = literal.end()
            continue
        if character in "([{":
            depth += 1
        elif character in ")]}":
            if depth == 0:
                arguments.append("".join(current).strip())
                return arguments
            depth -= 1
        elif character == "," and depth == 0:
            arguments.append("".join(current).strip())
            current = []
            index += 1
            continue
        current.append(character)
        index += 1
    raise RouteError(f"{where}: unterminated CRUX_WEB_ROUTE")


def literal(argument):
    match = re.fullmatch(r'"((?:[^"\\])*)"', argument)
    return match.group(1) if match else None


def check_path(path, where):
    if not path.startswith("/"):
        raise RouteError(f"{where}: path {path!r} must start with /")
    if path == "/":
        return
    params = 0
    for segment in path[1:].split("/"):
        if PARAM_SEGMENT.match(segment):
            params += 1
        elif not FIXED_SEGMENT.match(segment):
            raise RouteError(f"{where}: path {path!r} has a segment that is not plain text or a "
                             "whole-segment {parameter}")
        elif segment in (".", ".."):
            raise RouteError(f"{where}: path {path!r} has a dot segment")
    if params > PATH_PARAMS_MAX:
        raise RouteError(f"{where}: path {path!r} has more than {PATH_PARAMS_MAX} parameters")


def parse_methods(argument, where):
    methods = [token.strip() for token in argument.split("|")]
    for method in methods:
        if method not in METHODS:
            raise RouteError(f"{where}: methods must be CRUX_WEB_* bits joined with |, not {argument!r}")
    if len(set(methods)) != len(methods):
        raise RouteError(f"{where}: repeated method in {argument!r}")
    return tuple(methods)


def parse_access(argument, where):
    if argument in (ACCESS_SESSION, ACCESS_PUBLIC):
        return argument
    permission = literal(argument)
    if permission is None or not PERMISSION.match(permission):
        raise RouteError(f"{where}: a route needs an access class: a permission name, "
                         f"CRUX_WEB_SESSION or CRUX_WEB_PUBLIC, not {argument!r}")
    return permission


def check_bundle_form(clean, source):
    """Refuse a bundle source that holds anything but the forms that web_assets.py writes."""
    start = BUNDLE_PREAMBLE.match(clean)
    if start is None:
        raise RouteError(f"{source}: a packaged web folder must start with the two lines that "
                         "web_assets.py writes, the style define and the include of web_assets_internal.h")
    position = start.end()
    while True:
        while position < len(clean) and clean[position].isspace():
            position += 1
        if position == len(clean):
            return
        for statement in BUNDLE_STATEMENTS:
            found = statement.match(clean, position)
            if found:
                position = found.end()
                break
        else:
            line = clean.count("\n", 0, position) + 1
            raise RouteError(f"{source}:{line}: a packaged web folder may hold only what web_assets.py "
                             "writes: data, header lists, the table, its count and asset routes, with "
                             "no other preprocessor line or statement")


def parse_source(text, source, bundle=False):
    """Return the routes in one source file.

    A bundle source is generated by components/web/tools/web_assets.py. Each route in it must be a
    public GET of a fixed path outside /api, in the plain CRUX_WEB_ROUTE form, named
    crux_web_asset_route_*, with the handler crux_web_assets_serve and an element of
    crux_web_asset_table as its context. A source named as a bundle therefore cannot carry any other
    route or handler past the single review line.
    """
    clean = strip_comments(text)
    if bundle:
        check_bundle_form(clean, source)
    # Directives span continued lines. Blanks of the same length keep every offset, for line numbers.
    joined = CONTINUATION.sub(lambda match: " " * len(match.group(0)), clean)
    redefined = REDEFINED.search(joined)
    if redefined:
        line = clean.count("\n", 0, redefined.start()) + 1
        raise RouteError(f"{source}:{line}: a source of routes may not {redefined.group(1)} "
                         f"{redefined.group(2)}, which would make the table differ from this scan")
    routes = []
    for match in MACRO.finditer(blank_strings(joined)):
        line = clean.count("\n", 0, match.start()) + 1
        where = f"{source}:{line}"
        if re.match(r"\s*#\s*define\b", joined[joined.rfind("\n", 0, match.start()) + 1:match.start()]):
            raise RouteError(f"{where}: a macro that defines routes cannot be read")
        arguments = split_arguments(joined, match.end(), where)
        expected = 7 if match.group(1) else 6
        if len(arguments) != expected:
            raise RouteError(f"{where}: CRUX_WEB_ROUTE takes {expected} arguments, with a mandatory "
                             f"access class, and this has {len(arguments)}")
        name, path_argument, methods_argument, access_argument = arguments[:4]
        if not ROUTE_NAME.match(name):
            raise RouteError(f"{where}: bad route name {name!r}")
        path = literal(path_argument)
        if path is None:
            raise RouteError(f"{where}: the path must be one string literal, not {path_argument!r}")
        check_path(path, where)
        route = Route(name, path, parse_methods(methods_argument, where),
                      parse_access(access_argument, where), str(source), line, bundle)
        if bundle and (not name.startswith(BUNDLE_ROUTE_PREFIX) or route.access != ACCESS_PUBLIC or
                       route.methods != ("CRUX_WEB_GET",) or route.wildcard or match.group(1) or
                       arguments[4] != BUNDLE_HANDLER or not BUNDLE_CONTEXT.match(arguments[5]) or
                       path == "/api" or path.startswith("/api/")):
            raise RouteError(f"{where}: a packaged web folder may define only public GET routes of "
                             f"fixed paths outside /api, named {BUNDLE_ROUTE_PREFIX}*, with the handler "
                             f"{BUNDLE_HANDLER} and an element of crux_web_asset_table as context")
        routes.append(route)
    return routes


def parse_sources(paths, bundle=()):
    routes = []
    for path in paths:
        routes.extend(parse_source(Path(path).read_text(encoding="utf-8"), path))
    for path in bundle:
        routes.extend(parse_source(Path(path).read_text(encoding="utf-8"), path, bundle=True))
    return routes


def overlap(first, second):
    """Whether two wildcard patterns of the same depth can both match one path."""
    for left, right in zip(first.split("/"), second.split("/")):
        if left != right and left != "*" and right != "*":
            return False
    return True


def build_resources(routes):
    """Check the routes against each other, and return the resources in section order."""
    names, taken, resources = {}, {}, {}
    for route in routes:
        if route.name in names:
            raise RouteError(f"{route.source}:{route.line}: route name {route.name} is also at "
                             f"{names[route.name]}")
        names[route.name] = f"{route.source}:{route.line}"
        for method in route.methods:
            key = (route.pattern, method)
            if key in taken:
                raise RouteError(f"{route.source}:{route.line}: {method} {route.path} repeats "
                                 f"{taken[key]}")
            taken[key] = f"{route.source}:{route.line}"
        resources.setdefault(route.pattern, Resource(route.pattern)).methods.update(route.methods)

    wildcards = [resource for resource in resources.values() if resource.wildcard]
    for index, first in enumerate(wildcards):
        for second in wildcards[index + 1:]:
            if first.depth == second.depth and overlap(first.pattern, second.pattern):
                raise RouteError(f"wildcard paths {first.pattern} and {second.pattern} can match the "
                                 "same request")
    return sorted(resources.values(),
                  key=lambda resource: (not resource.wildcard, -resource.depth, resource.pattern))


def sorted_routes(routes):
    """Fixed routes before parameter routes, which is the order the core looks them up in."""
    return sorted(routes, key=lambda route: (route.wildcard, route.path, route.name))


GENERATED = "/* Generated by components/web/tools/web_routes.py. Do not edit. */\n"


def render_access(access):
    return access if access in (ACCESS_SESSION, ACCESS_PUBLIC) else f'"{access}"'


def render_table(routes):
    ordered = sorted_routes(routes)
    lines = [GENERATED, "#include <crux/web.h>", ""]
    lines.extend(f"extern const crux_web_route_t crux_web_route_{route.name};" for route in ordered)
    lines.append("")
    lines.append("extern const crux_web_route_t *const crux_web_route_table[];")
    lines.append("extern const crux_web_route_t crux_web_route_expected[];")
    lines.append("extern const size_t crux_web_route_count;")
    lines.append("")
    # What this scan read, which the core compares with what the compiler made, at start.
    lines.append("const crux_web_route_t crux_web_route_expected[] = {")
    for route in ordered:
        methods = " | ".join(route.methods)
        lines.append(f'    {{"{route.path}", {methods}, {render_access(route.access)}, NULL, NULL, 0u}},')
    lines.append("};")
    lines.append("")
    if ordered:
        lines.append("const crux_web_route_t *const crux_web_route_table[] = {")
        lines.extend(f"    &crux_web_route_{route.name}," for route in ordered)
        lines.append("};")
    else:
        lines.append("const crux_web_route_t *const crux_web_route_table[] = {NULL};")
    lines.append(f"const size_t crux_web_route_count = {len(ordered)};")
    return "\n".join(lines) + "\n"


def render_zephyr(resources):
    lines = [GENERATED, '#include "crux_web_zephyr_resources.h"', ""]
    for index, resource in enumerate(resources):
        mask = " | ".join(f"BIT({METHODS[method]})" for method in resource.sorted_methods())
        lines.append(f"CRUX_WEB_ZEPHYR_RESOURCE({index:04d}, \"{resource.pattern}\", {mask});")
    return "\n".join(lines) + "\n"


def render_manifest(resources):
    """The same resources for the Host's fake transport, in the same order."""
    lines = [GENERATED, '#include "fake_transport.h"', ""]
    lines.append("const fake_resource_spec_t fake_resource_specs[] = {")
    for resource in resources:
        mask = " | ".join(resource.sorted_methods())
        lines.append(f'    {{"{resource.pattern}", {mask}}},')
    lines.append("};")
    lines.append(f"const size_t fake_resource_spec_count = {len(resources)};")
    return "\n".join(lines) + "\n"


def review_lines(routes):
    """One line per method of each route that a person must review: public and session-only.

    The routes of a packaged web folder share the one line BUNDLE_REVIEW.
    """
    lines = []
    if any(route.bundle for route in routes):
        lines.append(BUNDLE_REVIEW)
    for route in routes:
        if route.bundle:
            continue
        if route.access_class in ("public", "session"):
            for method in route.methods:
                lines.append(f"{route.access_class} {method.removeprefix('CRUX_WEB_')} {route.path}")
    return sorted(lines)


def read_reviewed(path):
    entries = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.append(" ".join(line.split()))
    return sorted(entries)


def review_errors(routes, reviewed):
    found, expected = set(review_lines(routes)), set(read_reviewed(reviewed))
    return ([f"not reviewed: {entry}" for entry in sorted(found - expected)] +
            [f"reviewed, but no such route: {entry}" for entry in sorted(expected - found)])


def write_if_changed(path, text):
    path = Path(path)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sources", nargs="*")
    parser.add_argument("--table", required=True, help="Output: the portable route table")
    parser.add_argument("--zephyr", help="Output: the registered Zephyr resources")
    parser.add_argument("--manifest", help="Output: the resources for the Host's fake transport")
    parser.add_argument("--bundle", action="append", default=[],
                        help="A source that web_assets.py generated, whose routes are one reviewed entry")
    parser.add_argument("--reviewed", help="The reviewed list of public and session-only routes")
    args = parser.parse_args(argv)
    try:
        routes = parse_sources(args.sources, args.bundle)
        if not routes:
            raise RouteError("no CRUX_WEB_ROUTE in the named sources")
        resources = build_resources(routes)
        if args.reviewed:
            errors = review_errors(routes, args.reviewed)
            if errors:
                raise RouteError("; ".join(errors))
    except (RouteError, OSError) as error:
        print(f"web_routes: {error}", file=sys.stderr)
        return 1
    write_if_changed(args.table, render_table(routes))
    if args.zephyr:
        write_if_changed(args.zephyr, render_zephyr(resources))
    if args.manifest:
        write_if_changed(args.manifest, render_manifest(resources))
    return 0


if __name__ == "__main__":
    sys.exit(main())
