until mkdir /private/tmp/crux-build.lock 2>/dev/null; do sleep 20; done; echo $$ > /private/tmp/crux-build.lock/owner
W=<workspace>
docker run --rm --name crux-c4-fmt --memory 1g --cpus 2 --platform linux/amd64 \
  -v $W/.worktrees/rt1060-c4:/ws/crux-pilot \
  -v $W/crux-pilot/.git:$W/crux-pilot/.git:ro \
  -v $W/.worktrees/rt1060-build-c4/scripts:/scripts:ro \
  -v crux-host-cache:/opt/crux-cache \
  crux-pilot-devcontainer:local bash /scripts/c4_fmt_host.sh > $W/.worktrees/rt1060-build-c4/c4_fmt_host.log 2>&1
echo "container exit $?" >> $W/.worktrees/rt1060-build-c4/c4_fmt_host.log
rm -rf /private/tmp/crux-build.lock
