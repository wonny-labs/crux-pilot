# usage: native.sh APP_DIR NAME   native_sim build of a test app, then a run with -no-rt, output to /build/NAME.run.txt
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr
export PYTHONDONTWRITEBYTECODE=1
src=$1; name=$2
rm -rf /build/$name
cmake -S $src -B /build/$name -G Ninja -DBOARD='native_sim/native/64' -DZEPHYR_MODULES=/ws/crux-pilot -DPython3_EXECUTABLE=$(command -v python3) > /build/$name.configure.log 2>&1 || { tail -40 /build/$name.configure.log; echo CONFIGURE-FAILED; exit 1; }
ninja -C /build/$name -j2 > /build/$name.build.log 2>&1 || { grep -E "error|FAILED|undefined|warning" /build/$name.build.log | head -30; echo BUILD-FAILED; exit 1; }
timeout 120 /build/$name/zephyr/zephyr.exe -no-rt > /build/$name.run.txt 2>&1 || echo "run exit $?"
wc -l /build/$name.run.txt
