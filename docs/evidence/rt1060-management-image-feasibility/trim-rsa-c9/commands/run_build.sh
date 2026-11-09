# usage: run_build.sh SRC_WORKTREE NAME [cmake args...]   takes the build lock, runs one build in a container, releases it
W=<workspace>
S=<scratch folder outside any repository>
src=$1; name=$2; shift 2
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
docker run --rm --name crux-c9-bench --memory 2g --cpus 2 --platform linux/amd64 \
  -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 -e CRUX_WORKSPACE=/ws -e ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
  -v $src:/ws/crux-pilot:ro \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/rt1060-build-c1/zephyr-wolf:/ws/zephyr-wolf:ro \
  -v $W/.worktrees/stage1-ws/modules/hal:/ws/modules/hal:ro \
  -v $W/.worktrees/stage1-ws/modules/fs:/ws/modules/fs:ro \
  -v $W/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v $W/.worktrees/rt1060-build-c9:/build \
  -v $S/secret:/secret:ro \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local bash /build/scripts/c9_build.sh $name "$@" > $W/.worktrees/rt1060-build-c9/$name.run.log 2>&1
echo "container exit $?" >> $W/.worktrees/rt1060-build-c9/$name.run.log
rmdir /private/tmp/crux-build.lock
