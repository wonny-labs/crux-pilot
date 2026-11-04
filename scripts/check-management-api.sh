#!/usr/bin/env bash
# Lint the management API specification with the tools pinned in web/api/lint.
# The tools go in the Crux cache, so none of them enter the source tree.
#
# Usage: scripts/check-management-api.sh [specification]
# Set CRUX_CACHE_DIR to move the cache from its default beside the repository.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
spec="${1:-$repo/web/api/openapi.yaml}"
lint="$repo/web/api/lint"

if ! command -v node >/dev/null || ! command -v npm >/dev/null; then
    printf 'check-management-api: node and npm are required\n' >&2
    exit 2
fi

wanted="$(node -p 'require(process.argv[1]).engines.node' "$lint/package.json")"
actual="$(node -p 'process.versions.node')"
if [[ "$actual" != "$wanted" ]]; then
    printf 'check-management-api: Node.js %s is pinned, found %s\n' "$wanted" "$actual" >&2
    exit 2
fi

key="$(node -e '
const crypto = require("crypto");
const fs = require("fs");
const hash = crypto.createHash("sha256");
for (const file of process.argv.slice(1)) {
    hash.update(fs.readFileSync(file));
}
console.log(hash.digest("hex").slice(0, 16));
' "$lint/package.json" "$lint/package-lock.json")"
cache="${CRUX_CACHE_DIR:-$repo/../.crux-cache}"
tools="$cache/tools/management-api-lint-$key"
spectral="$tools/node_modules/.bin/spectral"

if [[ ! -x "$spectral" ]]; then
    mkdir -p "$cache/tools"
    stage="$(mktemp -d "$cache/tools/management-api-lint-stage.XXXXXX")"
    trap 'rm -rf "$stage"' EXIT
    cp "$lint/package.json" "$lint/package-lock.json" "$stage/"
    # Lifecycle scripts stay off, and npm ci fails if the lockfile does not match.
    npm ci --prefix "$stage" --ignore-scripts --no-audit --no-fund --loglevel=error
    rm -rf "$tools"
    mv "$stage" "$tools"
    trap - EXIT
fi

"$spectral" lint "$spec" --ruleset "$lint/spectral.yaml" --fail-severity=warn
printf 'check-management-api: %s passed with Spectral %s on Node.js %s\n' \
    "$spec" "$("$spectral" --version)" "$actual"
