#!/usr/bin/env bash
# Historical entry point; the maintained controller integration runner is renamed.
exec "$(dirname "${BASH_SOURCE[0]}")/test-controller-integration.sh" "$@"
