#!/usr/bin/env bash
# Note: this ran as build2.sh (the second run, which also kept the BIN and the device tree); it is committed here as build.sh.
S=<scratch>
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /private/tmp/crux-build.lock 2>/dev/null' EXIT
rm -rf $S/out/c8; echo "start $(date -u +%FT%TZ) free $(df -h / | awk 'NR==2{print $4}')" > $S/run.log
$S/scripts/container.sh <a clean detached checkout of the commit> bash /build/scripts/build.sh >> $S/run.log 2>&1
echo "container exit $? at $(date -u +%FT%TZ) free $(df -h / | awk 'NR==2{print $4}')" >> $S/run.log
