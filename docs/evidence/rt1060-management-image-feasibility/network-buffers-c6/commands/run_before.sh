#!/usr/bin/env bash
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
echo $$ > /private/tmp/crux-build.lock/owner
trap 'rmdir /private/tmp/crux-build.lock 2>/dev/null || rm -rf /private/tmp/crux-build.lock' EXIT
echo "start $(date +%T) free $(df -h / | awk 'NR==2{print $4}')"
<scratch>/container.sh before <workspace>/.worktrees/rt1060-bench-c6-base bash /build/scripts/build.sh before
echo "end $(date +%T)"
