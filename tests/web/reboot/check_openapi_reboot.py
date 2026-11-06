#!/usr/bin/env python3
"""Hold what the reboot routes answer to the management API specification.

The fixture prints real answers of the real routes. This script reads the schemas and the declared statuses
from the merged specification on every run, so a change to either reaches the check. It reuses the reader,
the validator and the record check of the configuration routes' script, which use the standard library only
and cover the subset of YAML and JSON Schema that the specification's schemas use, and it fails loudly on
anything else. The negative controls show that the check can fail.

    check_openapi_reboot.py --fixture EXECUTABLE --root REPOSITORY
"""
import argparse
import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CONFIG_CHECK = HERE.parent / "management" / "check_openapi_config.py"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


config_check = load("config_check", CONFIG_CHECK)
status_check = config_check.status_check
config_check.SUCCESS_SCHEMA.update({
    ("GET", "/api/v1/system/reboot", 200): "RebootState",
    ("POST", "/api/v1/system/reboot", 202): "RebootAccepted",
})
OPERATIONS = [("GET", "/api/v1/system/reboot"), ("POST", "/api/v1/system/reboot")]
SERVER_STATUS = {409}
REFUSED = "reboot_refused"
EXTRA_MEMBER_COMPLAINT = "an error form with more than a code and fields"


# What a mistake would produce. Each must be refused, or this check proves nothing.
NEGATIVE_CONTROLS = {
    "a state with no state": ("RebootState", {}),
    "a state of its own": ("RebootState", {"state": "rebooting"}),
    "a refused state with no reason": ("RebootState", {"state": "refused"}),
    "a pending state with a reason": ("RebootState", {"state": "pending", "reason": "x"}),
    "a failed state with a reason": ("RebootState", {"state": "failed", "reason": "x"}),
    "a state with an extra member": ("RebootState", {"state": "none", "failure": "reset"}),
    "a reason past 63 characters": ("RebootState", {"state": "refused", "reason": "x" * 64}),
    "a reason that is not text": ("RebootState", {"state": "refused", "reason": 5}),
    "an accepted request that is not pending": ("RebootAccepted", {"state": "none"}),
    "an accepted request with a reason": ("RebootAccepted", {"state": "pending", "reason": "x"}),
    "an error with a code of its own": ("Error", {"code": "oops"}),
    "a refusal with a reason that is too long": ("Error", {"code": REFUSED, "reason": "x" * 64}),
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
    for schema, document in (("RebootState", {"state": "none"}), ("RebootState", {"state": "pending"}),
                             ("RebootState", {"state": "failed"}),
                             ("RebootState", {"state": "refused", "reason": "Machine cycle running"}),
                             ("RebootState", {"state": "refused", "reason": "x" * 63}),
                             ("RebootAccepted", {"state": "pending"}),
                             ("Error", {"code": REFUSED, "reason": "Machine cycle running"})):
        try:
            status_check.validate(document, {"$ref": f"#/components/schemas/{schema}"}, schemas)
        except status_check.Invalid as error:
            errors.append(f"positive control refused: {document}: {error}")
    return errors


def check(records, schemas, declared):
    errors = []
    seen = set()
    for number, record in enumerate(records):
        where = f"record {number} {record['method']} {record['template']} {record['status']}"
        operation = (record["method"], record["template"])
        if record["status"] not in declared.get(operation, set()):
            errors.append(f"{where}: the specification does not declare this status")
        seen.add((operation, record["status"]))
        found = []
        config_check.check_record(record, schemas, found, where)
        if record["status"] == 423:
            # The refusal is the one error form with a reason, which the Error schema holds.
            found = [line for line in found if EXTRA_MEMBER_COMPLAINT not in line]
            body = json.loads(record["body"])
            if body.get("code") != REFUSED or "reason" not in body or set(body) != {"code", "reason"}:
                found.append(f"{where}: a 423 that is not a reboot refusal with a reason")
        elif record["status"] >= 400 and "reason" in json.loads(record["body"]):
            found.append(f"{where}: a reason on an error that is not a refusal")
        errors += found
        if record["status"] == 200:
            body = json.loads(record["body"])
            if set(body) - {"state", "reason"}:
                errors.append(f"{where}: a member that the specification does not hold")
    bodies = [record["body"] for record in records]
    for operation in OPERATIONS:
        for status in sorted(declared.get(operation, set()) - SERVER_STATUS):
            if (operation, status) not in seen:
                errors.append(f"never reached: {operation[0]} {operation[1]} {status}")
    for needle in ('"state":"none"', '"state":"pending"', '"state":"refused","reason":"',
                   '"state":"failed"', '"code":"reboot_refused"', '"code":"unauthorized"',
                   '"code":"permission_denied"', '"code":"csrf_rejected"', '"code":"origin_rejected"',
                   '"code":"not_ready"', '"code":"busy"', '\\"no\\"', '\\\\run'):
        if not any(needle in body for body in bodies):
            errors.append(f"never seen: {needle}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    text = args.root.joinpath(*config_check.SPEC_PARTS).read_text(encoding="utf-8")
    schemas = config_check.all_schemas(text)
    declared = config_check.declared_statuses(text)
    errors = negative_controls(schemas)
    result, records = config_check.read_records(args.fixture)
    if result.returncode != 0:
        errors.append(f"the fixture failed:\n{result.stdout}")
    if len(records) < 20:
        errors.append(f"only {len(records)} records")
    errors += check(records, schemas, declared)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"{len(records)} answers of the reboot routes conform to the specification; "
          f"{len(NEGATIVE_CONTROLS)} negative controls refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
