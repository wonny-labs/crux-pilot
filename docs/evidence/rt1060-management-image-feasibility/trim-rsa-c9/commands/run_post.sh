# usage: run_post.sh SRC_WORKTREE SCRIPT_NAME   (a light container run under the lock)
W=<workspace>
src=$1; script=$2
until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done
docker run --rm --name crux-c9-post --memory 1g --cpus 2 --platform linux/amd64 \
  -e CRUX_WORKSPACE=/ws -e ZEPHYR_SDK_INSTALL_DIR=/opt/crux-cache/sdk/zephyr-sdk-1.0.1 \
  -v $src:/ws/crux-pilot:ro \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/stage1-ws/zephyr:/ws/zephyr:ro \
  -v $W/.worktrees/c1-crypto:/ws/modules/crypto:ro \
  -v $W/.worktrees/rt1060-build-c9:/build \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local bash /build/scripts/$script > $W/.worktrees/rt1060-build-c9/$script.log 2>&1
echo "container exit $?" >> $W/.worktrees/rt1060-build-c9/$script.log
rmdir /private/tmp/crux-build.lock
