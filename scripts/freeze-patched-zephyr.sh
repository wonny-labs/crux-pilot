#!/usr/bin/env bash
# Freezes the identity of the patched Zephyr copy of the RT1060-EVKB bench image, in two calls around the build
# (docs/rt1060-bench-freeze-and-capture-design.md, part 1). Software only: it reads the copy, the stock Zephyr checkout and
# the patch files, writes evidence files, and has no device, probe, network or programming path. The first run on a real build
# is not done yet, and nothing here registers the bench profile.
#
#   scripts/freeze-patched-zephyr.sh capture --copy DIR --out FILE [--marker-file NAME]
#       before the build: the marker text and the tree digest of the copy
#   scripts/freeze-patched-zephyr.sh freeze --copy DIR --zephyr DIR --patches DIR --manifest FILE \
#       --compile-commands FILE --pre FILE --out DIR --inputs FILE --workspace DIR
#       after the build: checks the copy against the declared block and the west projects of the inputs file (step 1,
#       record-build-provenance.py --capture-inputs; both options are required), re-derives it from the stock checkout, and writes
#       DIR/patched-zephyr.json with the files it names (DIR is <build>/patched-zephyr)
#
# `freeze` needs git, tar and patch (the commands of scripts/prepare-wolf-tls-zephyr.sh) and is meant for the Linux
# development container, where the copy and the build are. The frozen files are checked later by the image check of the
# bench profile (tests/target/bench_image.py) on the host that drives the board, which never needs the copy.
set -euo pipefail

usage() {
    printf 'Usage: scripts/freeze-patched-zephyr.sh capture|freeze [options]; try "capture --help" or "freeze --help"\n' >&2
}

case "${1:-}" in
    capture | freeze) ;;
    *)
        usage
        exit 2
        ;;
esac

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$here/../tests/target/patched_zephyr.py" "$@"
