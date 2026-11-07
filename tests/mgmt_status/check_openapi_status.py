#!/usr/bin/env python3
"""Hold what the status service writes to the status schema in web/api/openapi.yaml.

The schema is read from the merged specification each run, so a change to it reaches this check. It
uses the standard library only, because Host CI has no YAML package: it reads the indented subset the
schemas section uses, and it fails loudly on anything else. The negative controls show that the check
can fail.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def indent_of(line):
    return len(line) - len(line.lstrip(" "))


def scalar(text):
    text = text.strip()
    if text.startswith("[") and text.endswith("]"):
        return [scalar(item) for item in text[1:-1].split(",")]
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        return text[1:-1]
    if text == "true":
        return True
    if text == "false":
        return False
    if re.fullmatch(r"-?[0-9]+", text):
        return int(text)
    return text


def parse_block(lines, start, indent):
    """Parse a mapping or a list at exactly this indent, and return it and the next line."""
    index = start
    if lines[index].lstrip().startswith("- "):
        items = []
        while index < len(lines) and indent_of(lines[index]) == indent \
                and lines[index].lstrip().startswith("- "):
            body = lines[index].lstrip()[2:]
            if ":" in body and not body.startswith(("'", '"')):
                key, _, value = body.partition(":")
                items.append({key.strip(): scalar(value)})
            else:
                items.append(scalar(body))
            index += 1
        return items, index
    mapping = {}
    while index < len(lines) and indent_of(lines[index]) == indent:
        key, separator, value = lines[index].strip().partition(":")
        if not separator:
            raise ValueError(f"unexpected form: {lines[index]!r}")
        value = value.strip()
        index += 1
        if value in ("|", ">"):
            while index < len(lines) and indent_of(lines[index]) > indent:
                index += 1
            mapping[key] = ""
        elif value:
            mapping[key] = scalar(value)
        elif index < len(lines) and indent_of(lines[index]) > indent:
            mapping[key], index = parse_block(lines, index, indent_of(lines[index]))
        else:
            mapping[key] = None
    if index < len(lines) and indent_of(lines[index]) > indent:
        raise ValueError(f"unexpected indent: {lines[index]!r}")
    return mapping, index


def schemas_of(spec_text):
    """Return the status schemas under components.schemas, which reference nothing else."""
    lines = [line for line in spec_text.splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    index = lines.index("  schemas:") + 1
    schemas = {}
    while index < len(lines) and indent_of(lines[index]) >= 4:
        name = lines[index].strip().rstrip(":")
        body = index + 1
        while body < len(lines) and indent_of(lines[body]) > 4:
            body += 1
        if name.startswith("Status"):
            schemas[name], _ = parse_block(lines[index + 1:body], 0, 6)
        index = body
    return schemas


class Invalid(Exception):
    pass


def validate(value, schema, schemas, path="$"):
    if "$ref" in schema:
        name = schema["$ref"].rsplit("/", 1)[-1]
        return validate(value, schemas[name], schemas, path)
    if "oneOf" in schema:
        matches = 0
        reasons = []
        for option in schema["oneOf"]:
            try:
                validate(value, option, schemas, path)
                matches += 1
            except Invalid as error:
                reasons.append(str(error))
        if matches != 1:
            raise Invalid(f"{path}: matches {matches} of oneOf: {reasons}")
        return None
    kind = schema.get("type")
    if kind == "object":
        if not isinstance(value, dict):
            raise Invalid(f"{path}: not an object")
        for name in schema.get("required", []):
            if name not in value:
                raise Invalid(f"{path}: missing {name}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for name in value:
                if name not in properties:
                    raise Invalid(f"{path}: unexpected {name}")
        for name, item in value.items():
            if name in properties:
                validate(item, properties[name], schemas, f"{path}.{name}")
    elif kind == "array":
        if not isinstance(value, list):
            raise Invalid(f"{path}: not an array")
        for index, item in enumerate(value):
            validate(item, schema["items"], schemas, f"{path}[{index}]")
    elif kind == "string":
        if not isinstance(value, str):
            raise Invalid(f"{path}: not a string")
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            raise Invalid(f"{path}: longer than {schema['maxLength']}")
        if "pattern" in schema and not re.search(schema["pattern"], value):
            raise Invalid(f"{path}: fails {schema['pattern']}")
    elif kind == "integer":
        if isinstance(value, bool) or not isinstance(value, int):
            raise Invalid(f"{path}: not an integer")
        if "minimum" in schema and value < schema["minimum"]:
            raise Invalid(f"{path}: below {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            raise Invalid(f"{path}: above {schema['maximum']}")
    elif kind == "boolean":
        if not isinstance(value, bool):
            raise Invalid(f"{path}: not a boolean")
    else:
        raise Invalid(f"{path}: this check does not know the type {kind!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise Invalid(f"{path}: {value!r} is not in {schema['enum']}")
    return None


REPORT = {"$ref": "#/components/schemas/StatusReport"}

# What a mistake in the service would produce. Each must be refused, or this check proves nothing.
NEGATIVE_CONTROLS = {
    "u64 as a number": {"items": [{"name": "a", "status": "ok", "type": "u64", "value": 5}]},
    "u64 with a leading zero": {"items": [{"name": "a", "status": "ok", "type": "u64", "value": "05"}]},
    "u32 as text": {"items": [{"name": "a", "status": "ok", "type": "u32", "value": "5"}]},
    "u32 above its range": {"items": [{"name": "a", "status": "ok", "type": "u32", "value": 4294967296}]},
    "bool as a number": {"items": [{"name": "a", "status": "ok", "type": "bool", "value": 1}]},
    "text as a number": {"items": [{"name": "a", "status": "ok", "type": "text", "value": 1}]},
    "text over its bound": {"items": [{"name": "a", "status": "ok", "type": "text", "value": "x" * 64}]},
    "failed item with a type": {"items": [{"name": "a", "status": "io", "type": "u32"}]},
    "failed item with a value": {"items": [{"name": "a", "status": "io", "value": 1}]},
    "ok item with no value": {"items": [{"name": "a", "status": "ok"}]},
    "unknown status": {"items": [{"name": "a", "status": "crux_err_io"}]},
    "name out of its pattern": {"items": [{"name": "A B", "status": "io"}]},
    "extra field": {"items": [{"name": "a", "status": "io", "detail": "x"}]},
    "extra field in the report": {"items": [], "total": 0},
    "no list": {},
}


def check_documents(documents, schemas):
    errors = []
    types_seen = set()
    words_seen = set()
    empty_lists = 0
    names_seen = set()
    for number, document in enumerate(documents):
        try:
            validate(document, REPORT, schemas)
        except Invalid as error:
            errors.append(f"document {number}: {error}")
            continue
        if not document["items"]:
            empty_lists += 1
        for item in document["items"]:
            names_seen.add(item["name"])
            words_seen.add(item["status"])
            if "type" in item:
                types_seen.add(item["type"])
            elif item["status"] == "ok":
                errors.append(f"document {number}: {item['name']} is ok with no type")
    # A run that printed only part of what it should would prove less than it says.
    missing_types = {"bool", "u32", "u64", "text"} - types_seen
    if missing_types:
        errors.append(f"never seen: types {sorted(missing_types)}")
    failed_schema = schemas["StatusItemFailed"]["properties"]["status"]["enum"]
    missing_words = set(failed_schema) - words_seen
    if missing_words:
        errors.append(f"never seen: statuses {sorted(missing_words)}")
    if empty_lists < 2:
        errors.append("the fixture prints the empty list for no visible item and a forced change")
    for name in ("system.uptime_ms", "system.watchdog_state", "net.link", "app.longest_text"):
        if name not in names_seen:
            errors.append(f"never seen: {name}")
    return errors


def check_negative_controls(schemas):
    errors = []
    for what, document in NEGATIVE_CONTROLS.items():
        try:
            validate(document, REPORT, schemas)
        except Invalid:
            continue
        errors.append(f"negative control accepted: {what}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, help="The program that prints status reports")
    parser.add_argument("--spec", required=True, type=Path, help="web/api/openapi.yaml")
    arguments = parser.parse_args()

    schemas = schemas_of(arguments.spec.read_text(encoding="utf-8"))
    result = subprocess.run([arguments.fixture], stdout=subprocess.PIPE, check=False)
    if result.returncode != 0:
        print(result.stdout.decode(errors="replace"))
        print(f"fixture exit status {result.returncode}", file=sys.stderr)
        return 1
    # By the byte, because a text value can hold U+2028, which splitlines would split a string at.
    documents = [json.loads(line) for line in result.stdout.split(b"\n") if line]

    errors = check_negative_controls(schemas) + check_documents(documents, schemas)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"{len(documents)} reports and {len(NEGATIVE_CONTROLS)} negative controls match "
          f"{arguments.spec.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
