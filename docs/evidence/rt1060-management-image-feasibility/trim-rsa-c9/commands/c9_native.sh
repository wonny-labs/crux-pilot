# usage: c9_native.sh STEP   (native_sim only: no board is touched). STEP is one of
#   entropy-plain   scripts/test-wolf-entropy.sh all, unmodified
#   entropy-norsa   the same script with -DCONFIG_WOLFSSL_RSA=n added to its configure line (one sed, recorded)
#   ssh-norsa       scripts/test-ssh-integrated.sh all with the image built with CONFIG_WOLFSSL_RSA=n (its own hook,
#                   CRUX_SSH_TEST_EXTRA_CONF) and the build-local Zephyr copy of C1 in place of a new one
#   ssh-plain       the same, unmodified (a control, if the norsa run fails)
set -u
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export PYTHONDONTWRITEBYTECODE=1
cd /ws/crux-pilot
step=$1
K=/build/keep/native-$step
rm -rf $K; mkdir -p $K
case $step in
entropy-plain|entropy-norsa)
  out=/build/wolf-entropy-$step
  rm -rf $out
  script=scripts/test-wolf-entropy.sh
  if [ $step = entropy-norsa ]; then
    sed 's#-DBOARD=native_sim/native/64 \\#-DBOARD=native_sim/native/64 -DCONFIG_WOLFSSL_RSA=n \\#' scripts/test-wolf-entropy.sh > /build/test-wolf-entropy-norsa.sh
    diff scripts/test-wolf-entropy.sh /build/test-wolf-entropy-norsa.sh > $K/script.diff
    script=/build/test-wolf-entropy-norsa.sh
  fi
  CRUX_WOLF_ENTROPY_OUTPUT=$out bash $script all > $K/run.log 2>&1
  echo "exit $?" > $K/status.txt
  for v in runtime init-fails fallback guard; do
    cp $out/$v-config.txt $out/$v-run.txt $K/ 2>/dev/null
    grep -E '^CONFIG_WOLFSSL_RSA|CONFIG_WOLFSSL_RSA is not set' $out/$v/zephyr/.config > $K/$v-rsa-config.txt 2>/dev/null
    tail -15 $out/$v-build.log > $K/$v-build-tail.txt 2>/dev/null
  done
  rm -rf $out
  ;;
ssh-norsa|ssh-plain)
  out=/build/ssh-integrated-$step
  rm -rf $out; mkdir -p $out
  ln -s /ws/zephyr-wolf $out/zephyr-wolf
  script=scripts/test-ssh-integrated.sh
  if [ $step = ssh-norsa ]; then
    printf 'CONFIG_WOLFSSL_RSA=n\n' > /build/no-rsa.conf
    sed 's#"-DZEPHYR_MODULES=$modules" "-DPython3_EXECUTABLE#"-DZEPHYR_MODULES=$modules" "-DCRUX_SSH_TEST_EXTRA_CONF=/build/no-rsa.conf" "-DPython3_EXECUTABLE#' scripts/test-ssh-integrated.sh > /build/test-ssh-integrated-norsa.sh
    diff scripts/test-ssh-integrated.sh /build/test-ssh-integrated-norsa.sh > $K/script.diff
    script=/build/test-ssh-integrated-norsa.sh
  fi
  CRUX_SSH_INTEGRATED_OUTPUT=$out CRUX_SSH_INTEGRATED_JOBS=2 bash $script all > $K/run.log 2>&1
  echo "exit $?" > $K/status.txt
  cp $out/config.txt $out/size.txt $K/ 2>/dev/null
  grep -E 'WOLFSSL_RSA' $out/build/zephyr/.config > $K/rsa-config.txt 2>/dev/null
  ls $out | grep -v '^build$\|^zephyr-wolf$' > $K/output-files.txt
  for f in $out/*.json $out/*.txt; do [ -f "$f" ] && cp "$f" $K/; done 2>/dev/null
  tail -30 $out/build.log > $K/build-tail.txt 2>/dev/null
  if [ -x $out/build/zephyr/zephyr.exe ]; then
    nm -P $out/build/zephyr/zephyr.exe > $K/nm-P.txt 2>/dev/null
    cp $out/build/zephyr/zephyr.map $K/zephyr.map 2>/dev/null
    python3 -I /ws/crux-pilot/tests/zephyr/management_rt1060_compile/check_bench_rsa_symbols.py $out/build/zephyr/zephyr.exe > $K/rsa-symbol-check.txt 2>&1
    echo "check exit $?" >> $K/rsa-symbol-check.txt
  fi
  rm -rf $out
  ;;
esac
du -sh /build 2>/dev/null | tail -1
