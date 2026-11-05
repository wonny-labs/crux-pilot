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
PATH_PARAMS_MAX = 4


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


def parse_source(text, source):
    """Return the routes in one source file."""
    clean = strip_comments(text)
    redefined = REDEFINED.search(clean)
    if redefined:
        line = clean.count("\n", 0, redefined.start()) + 1
        raise RouteError(f"{source}:{line}: a source of routes may not {redefined.group(1)} "
                         f"{redefined.group(2)}, which would make the table differ from this scan")
    routes = []
    for match in MACRO.finditer(blank_strings(clean)):
        line = clean.count("\n", 0, match.start()) + 1
        where = f"{source}:{line}"
        if re.match(r"\s*#\s*define\b", clean[clean.rfind("\n", 0, match.start()) + 1:match.start()]):
            raise RouteError(f"{where}: a macro that defines routes cannot be read")
        arguments = split_arguments(clean, match.end(), where)
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
        routes.append(Route(name, path, parse_methods(methods_argument, where),
                            parse_access(access_argument, where), str(source), line))
    return routes


def parse_sources(paths):
    routes = []
    for path in paths:
        routes.extend(parse_source(Path(path).read_text(encoding="utf-8"), path))
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
    """One line per method of each route that a person must review: public and session-only."""
    lines = []
    for route in routes:
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
    parser.add_argument("sources", nargs="+")
    parser.add_argument("--table", required=True, help="Output: the portable route table")
    parser.add_argument("--zephyr", help="Output: the registered Zephyr resources")
    parser.add_argument("--manifest", help="Output: the resources for the Host's fake transport")
    parser.add_argument("--reviewed", help="The reviewed list of public and session-only routes")
    args = parser.parse_args(argv)
    try:
        routes = parse_sources(args.sources)
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
