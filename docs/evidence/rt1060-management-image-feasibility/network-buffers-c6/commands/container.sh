#!/usr/bin/env bash
# usage: container.sh NAME TREE SCRIPT [args] : one container, own name, 2 GiB, the tree mounted at /ws/crux-pilot
name=$1; tree=$2; shift 2
docker run --rm --name crux-c6-$name --memory 2g --cpus 2 --platform linux/amd64 \
  -e CRUX_WORKSPACE=/ws -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 -e ZEPHYR_TOOLCHAIN_VARIANT=${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr} \
  -e CRUX_BENCH_PASSWORD_FILE=/secret/pw \
  -v $tree:/ws/crux-pilot:${MODE:-ro} \
  -v <workspace>/crux-pilot/.git:<workspace>/crux-pilot/.git:ro \
  -v <workspace>/.worktrees/rt1060-build-c1/zephyr-wolf:/ws/zephyr-wolf:ro \
  -v <workspace>/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v <workspace>/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v <workspace>/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v <workspace>/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v <scratch>:/build \
  -v <folder outside any repository>:/secret:ro \
  -v crux-host-cache:/opt/crux-cache \
  -w /ws/crux-pilot \
  crux-pilot-devcontainer:local "$@"
