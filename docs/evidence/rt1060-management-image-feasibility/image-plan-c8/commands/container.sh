#!/usr/bin/env bash
# usage: container.sh TREE SCRIPT : one container of its own, 2 GiB, the tree at /ws/crux-pilot read-only
S=<scratch>
W=<workspace>
tree=$1; shift
docker run --rm --name crux-c8-build --memory 2g --cpus 2 --platform linux/amd64 \
  -e CRUX_WORKSPACE=/ws -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v $tree:/ws/crux-pilot:ro \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/rt1060-build-c1/zephyr-wolf:/ws/zephyr-wolf:ro \
  -v $W/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v $W/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v $W/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v $W/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v $S:/build \
  -v $S/secret:/secret:ro \
  -v crux-host-cache:/opt/crux-cache \
  -w /ws/crux-pilot \
  crux-pilot-devcontainer:local "$@"
