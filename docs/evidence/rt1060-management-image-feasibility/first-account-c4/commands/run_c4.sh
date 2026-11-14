# Runs c4_all.sh in the Dev Container under the build lock, and releases the lock.
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done; echo $$ > /private/tmp/crux-build.lock/owner
W=<workspace>
docker run --rm --name crux-c4-bench --memory 2g --cpus 2 --platform linux/amd64 \
  -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 -e CRUX_WORKSPACE=/ws -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v $W/.worktrees/rt1060-c4:/ws/crux-pilot:ro \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v $W/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v $W/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v $W/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v $W/.worktrees/rt1060-build-c4:/build \
  -v <folder outside any repository>:/secret:ro \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local bash /build/scripts/c4_all.sh > $W/.worktrees/rt1060-build-c4/c4_all.log 2>&1
echo "container exit $?" >> $W/.worktrees/rt1060-build-c4/c4_all.log
rm -rf /private/tmp/crux-build.lock
