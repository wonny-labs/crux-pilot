#!/usr/bin/env python3
"""Gathers the browser cases' result lines into one results file that names its revision (Issue #220).

web/e2e appends one JSON line per case, with its suite, target and outcome. This writes them as one document
with the revision, whether the tree had changes, the target and the profile, and the count of each outcome, so
that a retained result names what produced it and mock and native_sim results stay apart.

    collect_results.py <lines.jsonl> --target mock|native_sim --profile mbed|wolf|none --output <file>
"""
import argparse
import collections
import json
import pathlib
import subprocess
import sys

REPOSITORY = pathlib.Path(__file__).resolve().parents[4]


def revision():
    def git(*arguments):
        result = subprocess.run(["git", "-C", str(REPOSITORY), *arguments], capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else None
    head = git("rev-parse", "HEAD")
    status = git("status", "--porcelain")
    return {"revision": head or "unknown", "dirty": None if status is None else bool(status)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("lines", type=pathlib.Path)
    parser.add_argument("--target", required=True, choices=("mock", "native_sim"))
    parser.add_argument("--profile", required=True, choices=("mbed", "wolf", "none"))
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    entries = []
    if args.lines.exists():
        entries = [json.loads(line) for line in args.lines.read_text().splitlines() if line.strip()]
    wrong = [entry for entry in entries if entry.get("target") != args.target]
    counts = collections.Counter(entry.get("outcome") for entry in entries)
    document = {**revision(), "target": args.target, "profile": args.profile,
                "outcomes": dict(sorted(counts.items())), "cases": entries}
    args.output.write_text(json.dumps(document, indent=1) + "\n")
    print(f"{args.output}: {dict(sorted(counts.items()))}")
    if wrong:
        print(f"{len(wrong)} results of another target in {args.lines}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
