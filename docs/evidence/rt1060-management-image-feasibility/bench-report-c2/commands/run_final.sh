#!/bin/bash
# One hold of the shared build lock: the native_sim run and the bench image at the head of the clean snapshot worktree.
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
echo $$ > /private/tmp/crux-build.lock/owner
trap 'rm -rf /private/tmp/crux-build.lock' EXIT
cd <workspace>/.worktrees/rt1060-build-c2
free_ok() { [ "$(df -k / | awk 'NR==2{print $4}')" -gt 5242880 ]; }
echo "== native $(date +%T) free $(df -h / | awk 'NR==2{print $4}')"
ZEPHYR_TOOLCHAIN_VARIANT=host ./container.sh bash /build/scripts/native.sh /ws/crux-pilot/tests/native/bench_report bench_report_native || echo "native step failed"
rm -rf bench_report_native
if free_ok; then
  echo "== target c2 $(date +%T) free $(df -h / | awk 'NR==2{print $4}')"
  ./container.sh bash /build/scripts/target.sh bench_c2 /ws/crux-pilot/tests/zephyr/management_rt1060_compile || echo "target step failed"
  ./container.sh bash /build/scripts/post.sh bench_c2 > post_c2.log 2>&1 || echo "post failed"
  cp bench_c2/zephyr/zephyr.elf bench_c2.zephyr.elf 2>/dev/null || true
  ./container.sh bash /build/scripts/post2.sh > post2_c2.log 2>&1 || echo "post2 failed"
  rm -rf bench_c2
else
  echo "SKIPPED target c2: free disk under 5 GiB"
fi
echo "== done $(date +%T)"
