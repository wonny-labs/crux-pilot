#!/usr/bin/env bash
docker run --rm --name crux-c5-bench --memory 2g --cpus 2 --platform linux/amd64 \
  -e CRUX_WORKSPACE=/ws -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v <workspace>/.worktrees/rt1060-wolf-entropy-c5:/ws/crux-pilot \
  -v <workspace>/crux-pilot/.git:<workspace>/crux-pilot/.git:ro \
  -v <workspace>/.worktrees/rt1060-build-c1/zephyr-wolf:/ws/zephyr-wolf:ro \
  -v <workspace>/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v <workspace>/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v <workspace>/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v <workspace>/.worktrees/rt1060-build-c5:/build \
  -v crux-host-cache:/opt/crux-cache \
  -w /ws/crux-pilot \
  crux-pilot-devcontainer:local bash /build/scripts/bench.sh
