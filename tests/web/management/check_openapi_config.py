#!/usr/bin/env python3
"""Hold what the status and configuration routes answer to the management API specification.

The fixture prints real answers of the real routes. This script reads the schemas and the declared statuses
from the merged specification on every run, so a change to either reaches the check. It uses the standard
library only, because Host CI has no YAML package, and it reuses the reader and the validator of the status
suite's check, which cover the subset of YAML and of JSON Schema that the specification's schemas use. It
fails loudly on anything else. The negative controls show that the check can fail.

    check_openapi_config.py --fixture EXECUTABLE --root REPOSITORY
"""
import argparse
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
STATUS_CHECK = HERE.parent.parent / "mgmt_status" / "check_openapi_status.py"
SPEC_PARTS = ("web", "api", "openapi.yaml")


def load_status_check():
    spec = importlib.util.spec_from_file_location("status_check", STATUS_CHECK)
    module = importlib.util.module_from_spec(spec)
    sys.modules["status_check"] = module
    spec.loader.exec_module(module)
    return module


status_check = load_status_check()

# The operations that this fixture reaches, and what each success answers with.
SUCCESS_SCHEMA = {
    ("GET", "/api/v1/status", 200): "StatusReport",
    ("GET", "/api/v1/config", 200): "ConfigList",
    ("GET", "/api/v1/config/{name}", 200): "ConfigValue",
    ("PUT", "/api/v1/config/{name}", 200): "ConfigItem",
    ("POST", "/api/v1/config/save", 202): "SaveQueued",
}
# Statuses that the server sends before any of this code runs, so no route can produce them.
SERVER_STATUS = {409}
# Every status that the specification declares for these operations, which the fixture must reach.
CONFIG_OPERATIONS = [
    ("GET", "/api/v1/status"),
    ("GET", "/api/v1/config"),
    ("GET", "/api/v1/config/{name}"),
    ("PUT", "/api/v1/config/{name}"),
    ("POST", "/api/v1/config/save"),
]


def all_schemas(text):
    lines = [line for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    index = lines.index("  schemas:") + 1
    schemas = {}
    while index < len(lines) and status_check.indent_of(lines[index]) >= 4:
        name = lines[index].strip().rstrip(":")
        body = index + 1
        while body < len(lines) and status_check.indent_of(lines[body]) > 4:
            body += 1
        try:
            schemas[name], _ = status_check.parse_block(lines[index + 1:body], 0, 6)
        except ValueError:
            # A request schema may use a form that the reader does not know. Responses cannot.
            pass
        index = body
    return schemas


def declared_statuses(text):
    """The statuses that the specification declares for each operation, read from the paths section."""
    declared = {}
    path = method = None
    in_responses = False
    for line in text.splitlines():
        if re.match(r"^components:", line):
            break
        found = re.match(r"^  (/\S+):\s*$", line)
        if found:
            path, method, in_responses = found.group(1), None, False
            continue
        found = re.match(r"^    (get|put|post|delete):\s*$", line)
        if found and path:
            method, in_responses = found.group(1).upper(), False
            continue
        if re.match(r"^      responses:\s*$", line):
            in_responses = True
            continue
        found = re.match(r"^        '([0-9]{3})':\s*$", line)
        if found and in_responses and method:
            declared.setdefault((method, path), set()).add(int(found.group(1)))
        elif re.match(r"^      \S", line):
            in_responses = False
    return declared


def read_records(fixture):
    result = subprocess.run([fixture], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            check=False, timeout=120)
    records = []
    for line in result.stdout.splitlines():
        if line.startswith("RECORD "):
            parts = line.split(" ", 7)
            if len(parts) != 8:
                raise ValueError(f"malformed record: {line!r}")
            _, method, template, status, retry, security, content_type, body = parts
            records.append({"method": method, "template": template, "status": int(status),
                            "retry": retry, "security": security == "1", "type": content_type,
                            "body": body})
    return result, records


def check_record(record, schemas, errors, where):
    key = (record["method"], record["template"], record["status"])
    if not record["security"]:
        errors.append(f"{where}: a header of the security set is missing")
    if record["type"] != "application/json":
        errors.append(f"{where}: content type {record['type']}")
    if record["status"] == 503:
        if not re.fullmatch(r"[1-9][0-9]*", record["retry"]):
            errors.append(f"{where}: a 503 without whole seconds in Retry-After: {record['retry']}")
    elif record["retry"] != "-":
        errors.append(f"{where}: Retry-After on a {record['status']}")
    try:
        body = json.loads(record["body"])
    except ValueError:
        errors.append(f"{where}: the body is not JSON: {record['body']!r}")
        return
    schema = SUCCESS_SCHEMA.get(key, "Error")
    try:
        status_check.validate(body, {"$ref": f"#/components/schemas/{schema}"}, schemas)
    except status_check.Invalid as error:
        errors.append(f"{where}: {record['body']}: {error}")
    if schema == "Error" and (set(body) - {"code", "fields"}):
        errors.append(f"{where}: an error form with more than a code and fields")
    if schema == "Error" and record["status"] >= 400 and "code" not in body:
        errors.append(f"{where}: an error with no code")


def check(records, schemas, declared):
    errors = []
    seen = set()
    for number, record in enumerate(records):
        where = f"record {number} {record['method']} {record['template']} {record['status']}"
        operation = (record["method"], record["template"])
        if record["status"] not in declared.get(operation, set()):
            errors.append(f"{where}: the specification does not declare this status")
        seen.add((operation, record["status"]))
        check_record(record, schemas, errors, where)
    # A run that reached only part of the routes would prove less than it says.
    for operation in CONFIG_OPERATIONS:
        for status in sorted(declared.get(operation, set()) - SERVER_STATUS):
            if (operation, status) not in seen:
                errors.append(f"never reached: {operation[0]} {operation[1]} {status}")
    # The distinct forms of the parts that a list and an error can carry.
    bodies = [record["body"] for record in records]
    for needle in ('"state":"pending"', '"state":"saved"', '"code":"storage_failed"',
                   '"code":"not_supported"', '"code":"unauthorized"', '"code":"busy"',
                   '"type":"bool"', '"type":"u32"', '"type":"string"', '"sensitive":true',
                   '"readable":false', '"writable":false', '"unsaved":true', '"code":"wrong_type"',
                   '"code":"out_of_range"', '"code":"rule_not_met"', '"items":[]'):
        if not any(needle in body for body in bodies):
            errors.append(f"never seen: {needle}")
    return errors


def sample(**changes):
    item = {"name": "limit", "type": "u32", "activation": "application", "sensitive": False,
            "readable": True, "writable": True, "unsaved": False}
    item.update(changes)
    return item


# What a mistake would produce. Each must be refused, or this check proves nothing.
NEGATIVE_CONTROLS = {
    "an item with no name": ("ConfigItem", {k: v for k, v in sample().items() if k != "name"}),
    "an item with a value": ("ConfigItem", sample(value=5)),
    "an item with a type of its own": ("ConfigItem", sample(type="float")),
    "an item with an activation of its own": ("ConfigItem", sample(activation="now")),
    "an item with a flag as text": ("ConfigItem", sample(unsaved="false")),
    "a name with a space": ("ConfigItem", sample(name="a b")),
    "a name past 30 characters": ("ConfigItem", sample(name="x" * 31)),
    "a list with no items": ("ConfigList", {}),
    "a list with an extra member": ("ConfigList", {"items": [], "total": 0}),
    "a save of an unknown state": ("ConfigList", {"items": [], "save": {"state": "running"}}),
    "a failed save with no code": ("ConfigList", {"items": [], "save": {"state": "failed"}}),
    "a failed save with a code of its own": ("ConfigList",
                                             {"items": [], "save": {"state": "failed", "code": "disk"}}),
    "a pending save with a code": ("ConfigList",
                                   {"items": [], "save": {"state": "pending", "code": "busy"}}),
    "a u32 value as text": ("ConfigValue", {"name": "a", "type": "u32", "value": "5"}),
    "a u32 above its range": ("ConfigValue", {"name": "a", "type": "u32", "value": 4294967296}),
    "a bool as a number": ("ConfigValue", {"name": "a", "type": "bool", "value": 1}),
    "a string as a number": ("ConfigValue", {"name": "a", "type": "string", "value": 1}),
    "a value of the wrong type name": ("ConfigValue", {"name": "a", "type": "text", "value": "x"}),
    "an error with no code": ("Error", {}),
    "an error with a code of its own": ("Error", {"code": "oops"}),
    "a field error of its own": ("Error", {"code": "invalid_field",
                                           "fields": [{"name": "value", "code": "nope"}]}),
    "a field error with no name": ("Error", {"code": "invalid_field", "fields": [{"code": "wrong_type"}]}),
    "a save that is not pending": ("SaveQueued", {"state": "saved"}),
}


def negative_controls(schemas):
    errors = []
    for name, (schema, document) in NEGATIVE_CONTROLS.items():
        try:
            status_check.validate(document, {"$ref": f"#/components/schemas/{schema}"}, schemas)
        except status_check.Invalid:
            continue
        errors.append(f"negative control accepted: {name}")
    # And a positive one for each, so that a control refused for another reason shows.
    for schema, document in (("ConfigItem", sample()), ("ConfigItem", sample(name="x" * 30)),
                             ("ConfigList", {"items": [sample()], "save": {"state": "saved"}}),
                             ("ConfigList", {"items": [], "save": {"state": "failed", "code": "busy"}}),
                             ("ConfigValue", {"name": "a", "type": "u32", "value": 4294967295}),
                             ("Error", {"code": "invalid_field",
                                        "fields": [{"name": "value", "code": "out_of_range"}]})):
        try:
            status_check.validate(document, {"$ref": f"#/components/schemas/{schema}"}, schemas)
        except status_check.Invalid as error:
            errors.append(f"positive control refused: {document}: {error}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    text = args.root.joinpath(*SPEC_PARTS).read_text(encoding="utf-8")
    schemas = all_schemas(text)
    declared = declared_statuses(text)
    errors = negative_controls(schemas)
    result, records = read_records(args.fixture)
    if result.returncode != 0:
        errors.append(f"the fixture failed:\n{result.stdout}")
    if len(records) < 50:
        errors.append(f"only {len(records)} records")
    errors += check(records, schemas, declared)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"{len(records)} answers of the status and configuration routes conform to the specification; "
          f"{len(NEGATIVE_CONTROLS)} negative controls refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
