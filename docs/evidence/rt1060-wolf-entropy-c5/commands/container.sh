#!/usr/bin/env bash
# One container run of the C5 Wolf entropy test. Usage: pr-c5-container.sh <command...>
docker run --rm --name crux-c5-entropy --memory 2g --cpus 2 --platform linux/amd64 \
  -e CRUX_WORKSPACE=/ws -e CRUX_WOLF_ENTROPY_OUTPUT=/build/wolf-entropy \
  -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 \
  -v <workspace>/.worktrees/rt1060-wolf-entropy-c5:/ws/crux-pilot \
  -v <workspace>/crux-pilot/.git:<workspace>/crux-pilot/.git:ro \
  -v <workspace>/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v <workspace>/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v <workspace>/.worktrees/rt1060-build-c5:/build \
  -v crux-host-cache:/opt/crux-cache \
  -w /ws/crux-pilot \
  crux-pilot-devcontainer:local bash -c 'export PATH=/opt/crux-cache/venv/bin:$PATH; git config --global --add safe.directory "*"; exec "$@"' _ "$@"
