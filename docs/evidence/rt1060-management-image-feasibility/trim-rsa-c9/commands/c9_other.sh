# usage: c9_other.sh NAME   a non-bench composition for the same board, to show that it builds unchanged:
# tests/zephyr/ssh_provider with rt1060.conf (wolfSSL, wolfSSH, RSA on), compile and link only, on the unpatched Zephyr.
set -u
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr PYTHONDONTWRITEBYTECODE=1
name=$1; K=/build/keep/other-$name; B=/build/other-$name
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
mkdir -p $K; rm -rf $B
cmake -S /ws/crux-pilot/tests/zephyr/ssh_provider -B $B -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DEXTRA_CONF_FILE=/ws/crux-pilot/tests/zephyr/ssh_provider/rt1060.conf -DPython3_EXECUTABLE=$(command -v python3) > $K/configure.log 2>&1
echo "configure exit $?" > $K/status.txt
ninja -C $B -j2 > $K/build.log 2>&1
echo "ninja exit $?" >> $K/status.txt
E=$B/zephyr/zephyr.elf
if [ -f $E ]; then
  $SDK/arm-zephyr-eabi-size $E | tee $K/size.txt
  $SDK/arm-zephyr-eabi-nm -S --size-sort -r $E > $K/nm-all.txt
  cp $B/zephyr/.config $K/config.txt
  sha256sum $B/zephyr/zephyr.bin > $K/sha256.txt
  grep -E 'FLASH:|RAM:' $K/build.log > $K/regions.txt
  grep -cE 'Rsa(\b|[A-Z_])|_rsa\b|sp_ModExp_3072' $K/nm-all.txt > $K/rsa-symbol-count.txt
  grep -E 'CONFIG_WOLFSSL_RSA' $K/config.txt >> $K/rsa-symbol-count.txt
fi
rm -rf $B
