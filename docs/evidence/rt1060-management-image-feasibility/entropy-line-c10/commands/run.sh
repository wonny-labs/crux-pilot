#!/usr/bin/env bash
# The scratch folder S is outside every repository. The commit under test is checked out clean and detached.
S=<scratch>
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /private/tmp/crux-build.lock 2>/dev/null' EXIT
rm -rf $S/out/c10; echo "start $(date -u +%FT%TZ) free $(df -h / | awk 'NR==2{print $4}')" > $S/run.log
$S/scripts/container.sh <a clean detached checkout of the commit> bash /build/scripts/build.sh >> $S/run.log 2>&1
echo "container exit $? at $(date -u +%FT%TZ) free $(df -h / | awk 'NR==2{print $4}')" >> $S/run.log
