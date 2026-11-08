#!/usr/bin/env python3
"""Hold what the network routes answer to the management API specification.

The fixtures print real answers of the real routes. This script reads the schemas and the declared statuses
from the merged specification on every run, so a change to either reaches the check. It reuses the reader,
the validator and the record check of the configuration routes' script, which use the standard library only
and cover the subset of YAML and JSON Schema that the specification's schemas use, and it fails loudly on
anything else. The negative controls show that the check can fail.

    check_openapi_network.py --fixture EXECUTABLE [--fixture EXECUTABLE ...] --root REPOSITORY
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
    ("GET", "/api/v1/network", 200): "NetworkState",
    ("PUT", "/api/v1/network", 202): "SaveQueued",
})
OPERATIONS = [("GET", "/api/v1/network"), ("PUT", "/api/v1/network")]
SERVER_STATUS = {409}


def settings(**changes):
    value = {"address": "192.0.2.10", "prefix_length": 24}
    value.update(changes)
    return value


def state(**changes):
    value = {"running": settings(), "source": "default", "reboot_required": False}
    value.update(changes)
    return value


# What a mistake would produce. Each must be refused, or this check proves nothing.
NEGATIVE_CONTROLS = {
    "a state with no source": ("NetworkState", {k: v for k, v in state().items() if k != "source"}),
    "a state with no reboot flag": ("NetworkState", {k: v for k, v in state().items() if k != "reboot_required"}),
    "a source of its own": ("NetworkState", state(source="factory")),
    "a reboot flag as text": ("NetworkState", state(reboot_required="false")),
    "a state with an extra member": ("NetworkState", state(mtu=1500)),
    "running with no address": ("NetworkState", state(running={"prefix_length": 24})),
    "running with a prefix length of 31": ("NetworkState", state(running=settings(prefix_length=31))),
    "running with a prefix length as text": ("NetworkState", state(running=settings(prefix_length="24"))),
    "saved with an extra member": ("NetworkState", state(saved=settings(dns="192.0.2.1"))),
    "a pending save with a code": ("NetworkState", state(save={"state": "pending", "code": "busy"})),
    "a save that is saved": ("NetworkState", state(save={"state": "saved"})),
    "a save of an unknown state": ("NetworkState", state(save={"state": "running"})),
    "a queued save that is not pending": ("SaveQueued", {"state": "saved"}),
    "a queued save with an address": ("SaveQueued", {"state": "pending", "address": "192.0.2.10"}),
    "a field error of its own": ("Error", {"code": "invalid_field",
                                           "fields": [{"name": "gateway", "code": "gateway_not_usable"}]}),
    "a field error with no name": ("Error", {"code": "invalid_field", "fields": [{"code": "not_unicast"}]}),
    "an error with a code of its own": ("Error", {"code": "oops"}),
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
    for schema, document in (("NetworkState", state()),
                             ("NetworkState", state(saved=settings(gateway="192.0.2.1"), reboot_required=True,
                                                    save={"state": "failed"})),
                             ("NetworkState", {"source": "saved", "reboot_required": False}),
                             ("SaveQueued", {"state": "pending"}),
                             ("Error", {"code": "invalid_field",
                                        "fields": [{"name": "gateway", "code": "gateway_outside_subnet"}]})):
        try:
            status_check.validate(document, {"$ref": f"#/components/schemas/{schema}"}, schemas)
        except status_check.Invalid as error:
            errors.append(f"positive control refused: {document}: {error}")
    return errors


def check(records, schemas, declared, expect_501):
    errors = []
    seen = set()
    for number, record in enumerate(records):
        where = f"record {number} {record['method']} {record['template']} {record['status']}"
        operation = (record["method"], record["template"])
        if record["status"] not in declared.get(operation, set()):
            errors.append(f"{where}: the specification does not declare this status")
        seen.add((operation, record["status"]))
        config_check.check_record(record, schemas, errors, where)
        if record["status"] == 200:
            body = json.loads(record["body"])
            # What GET never carries: a value that a session may not read, or a field of this device.
            if set(body) - {"running", "saved", "source", "reboot_required", "save"}:
                errors.append(f"{where}: a member that the specification does not hold")
    bodies = [record["body"] for record in records]
    if expect_501:
        if (("PUT", "/api/v1/network"), 501) not in seen:
            errors.append("never reached: PUT /api/v1/network 501")
        return errors
    for operation in OPERATIONS:
        for status in sorted(declared.get(operation, set()) - SERVER_STATUS - {501}):
            if (operation, status) not in seen:
                errors.append(f"never reached: {operation[0]} {operation[1]} {status}")
    for needle in ('"state":"pending"', '"state":"failed"', '"source":"saved"', '"source":"default"',
                   '"reboot_required":true', '"reboot_required":false', '"saved":{', '"gateway":"',
                   '"code":"unauthorized"', '"code":"permission_denied"', '"code":"busy"',
                   '"code":"not_ready"', '"code":"bad_request"', '"code":"invalid_field"',
                   '"code":"payload_too_large"', '"code":"not_unicast"', '"code":"prefix_not_usable"',
                   '"code":"gateway_outside_subnet"'):
        if not any(needle in body for body in bodies):
            errors.append(f"never seen: {needle}")
    # A GET with no running values exists, and one with running values exists.
    gets = [json.loads(r["body"]) for r in records if r["method"] == "GET" and r["status"] == 200]
    if not any("running" not in body for body in gets):
        errors.append("never seen: a state with no running values")
    if not any("running" in body for body in gets):
        errors.append("never seen: a state with running values")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fixture", required=True, action="append")
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    text = args.root.joinpath(*config_check.SPEC_PARTS).read_text(encoding="utf-8")
    schemas = config_check.all_schemas(text)
    declared = config_check.declared_statuses(text)
    errors = negative_controls(schemas)
    total = 0
    for fixture in args.fixture:
        result, records = config_check.read_records(fixture)
        if result.returncode != 0:
            errors.append(f"the fixture {fixture} failed:\n{result.stdout}")
        total += len(records)
        no_provider = fixture.endswith("no_provider")
        if len(records) < (3 if no_provider else 20):
            errors.append(f"only {len(records)} records from {fixture}")
        errors += check(records, schemas, declared, no_provider)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"{total} answers of the network routes conform to the specification; "
          f"{len(NEGATIVE_CONTROLS)} negative controls refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
