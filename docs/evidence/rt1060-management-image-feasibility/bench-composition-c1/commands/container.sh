docker run --rm --name crux-c1-bench --memory 2g --cpus 2 --platform linux/amd64 \
  -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 \
  -e CRUX_WORKSPACE=/ws -e KEEP=${KEEP:-} -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v <workspace>/.worktrees/rt1060-bench-c1:/ws/crux-pilot \
  -v <workspace>/crux-pilot/.git:<workspace>/crux-pilot/.git:ro \
  -v <workspace>/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v <workspace>/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v <workspace>/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v <workspace>/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v <workspace>/.worktrees/rt1060-build-c1:/build \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local "$@"
