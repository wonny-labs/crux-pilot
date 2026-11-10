#!/usr/bin/env bash
W=<workspace>
S=<scratch>
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
trap 'rmdir /private/tmp/crux-build.lock 2>/dev/null' EXIT
rm -rf $S/keep $S/out; mkdir -p $S/keep $S/out
echo "start $(date -u +%FT%TZ) free $(df -h / | awk 'NR==2{print $4}')" > $S/run.log
BEFORE_TREE=$S/tree-before $S/scripts/container.sh crux-c11-build $S/tree-after bash /build/scripts/all.sh >> $S/run.log 2>&1
echo "container exit $? at $(date -u +%FT%TZ) free $(df -h / | awk 'NR==2{print $4}')" >> $S/run.log
