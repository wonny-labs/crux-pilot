# Container for the RT1060 feasibility builds (compile and link only; no device access)
W=<workspace>
docker run --rm --name crux-rt1060-feas --memory 2g --cpus 2 --platform linux/amd64 \
  -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 \
  -e CRUX_WORKSPACE=/ws -e KEEP=${KEEP:-} -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v $W/.worktrees/rt1060-mgmt-feasibility:/ws/crux-pilot \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v $W/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v $W/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v $W/.worktrees/rt1060-crypto:/ws/modules/crypto:ro \
  -v $W/.worktrees/rt1060-build:/build \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local "$@"
