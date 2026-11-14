#!/usr/bin/env bash
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
echo $$ > /private/tmp/crux-build.lock/owner
trap 'rm -rf /private/tmp/crux-build.lock' EXIT
T=<workspace>/.worktrees/rt1060-bench-c6
echo "native start $(date +%T) free $(df -h / | awk 'NR==2{print $4}')"
ZEPHYR_TOOLCHAIN_VARIANT=host <scratch>/container.sh native $T bash /build/scripts/native.sh /ws/crux-pilot/tests/native/bench_report bench_report_native
rm -rf <scratch>/bench_report_native
echo "target start $(date +%T) free $(df -h / | awk 'NR==2{print $4}')"
<scratch>/container.sh after $T bash /build/scripts/build.sh after
echo "end $(date +%T)"
