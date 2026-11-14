# usage: nbuild.sh NAME SOURCE_DIR   (native_sim/native/64 build of a Ztest, then run it)
set -e
name=$1; src=$2; shift 2
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr
M="/ws/crux-pilot;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf /build/$name
cmake -S $src -B /build/$name -G Ninja -DBOARD=native_sim/native/64 "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) "$@" > /build/$name.configure.log 2>&1 || { tail -40 /build/$name.configure.log; echo CONFIGURE-FAILED; exit 1; }
ninja -C /build/$name -j2 > /build/$name.build.log 2>&1 || { grep -E "error|FAILED|undefined" /build/$name.build.log | head -30; echo BUILD-FAILED; exit 1; }
/build/$name/zephyr/zephyr.exe > /build/$name.run.log 2>&1; echo "run exit $?"
tail -40 /build/$name.run.log
