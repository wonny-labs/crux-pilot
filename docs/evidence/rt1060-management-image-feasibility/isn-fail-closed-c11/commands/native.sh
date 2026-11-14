set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export PYTHONDONTWRITEBYTECODE=1
export CRUX_ISN_OUTPUT=/build/out/isn
cd /ws/crux-pilot
scripts/test-isn-fail-closed.sh "$@"
