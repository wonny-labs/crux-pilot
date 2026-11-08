#!/bin/bash
# usage: c3-run.sh LOGFILE SCRIPT ARGS...  (takes the build lock, runs one container, always releases the lock)
log=$1; shift
W=/Users/jiwonkim/Projects/crux-workspace
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
echo $$ > /private/tmp/crux-build.lock/owner
trap 'rm -rf /private/tmp/crux-build.lock; docker rm -f crux-c3-build >/dev/null 2>&1' EXIT
echo "locked $(date)" > "$log"; df -h /Users/jiwonkim | tail -1 >> "$log"
docker run --rm --name crux-c3-build --memory 2g --cpus 2 --platform linux/amd64 \
  -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 -e CRUX_WORKSPACE=/ws -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v $W/.worktrees/rt1060-bench-c3:/ws/crux-pilot \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v $W/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v $W/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v $W/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v $W/.worktrees/rt1060-build-c1/zephyr-wolf:/ws/zephyr-wolf:ro \
  -v $W/.worktrees/rt1060-build-c3:/build \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local bash "$@" >> "$log" 2>&1
echo "container exit $? $(date)" >> "$log"
