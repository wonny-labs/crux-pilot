# usage: run_native.sh SRC_WORKTREE STEP   takes the build lock, runs one native_sim step in a container, releases it
W=<workspace>
src=$1; step=$2
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
docker run --rm --name crux-c9-native --memory 2g --cpus 2 --platform linux/amd64 \
  -e CRUX_WORKSPACE=/ws -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 \
  -v $src:/ws/crux-pilot:ro \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v $W/.worktrees/rt1060-build-c1/zephyr-wolf:/ws/zephyr-wolf:ro \
  -v $W/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v $W/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v $W/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v $W/.worktrees/rt1060-build-c9:/build \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local bash /build/scripts/c9_native.sh $step > $W/.worktrees/rt1060-build-c9/native-$step.run.log 2>&1
echo "container exit $?" >> $W/.worktrees/rt1060-build-c9/native-$step.run.log
rmdir /private/tmp/crux-build.lock
