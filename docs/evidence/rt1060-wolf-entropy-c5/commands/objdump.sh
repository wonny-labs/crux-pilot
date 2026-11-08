set -e
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=/build/bench_c5/zephyr/zephyr.elf
for f in wc_GenerateSeed crux_entropy_boot_check; do
  echo "== calls made by $f"
  $SDK/arm-zephyr-eabi-objdump -d --no-show-raw-insn $E | awk -v f="<$f>:" '$2==f {on=1; next} on && /^$/ {on=0} on' | grep -E "\sbl\s|\sb\.w\s|\sb\s" | sed 's/.*\t//' | sort | uniq -c
done
echo "== who calls sys_rand_get / sys_csrand_get"
$SDK/arm-zephyr-eabi-objdump -d --no-show-raw-insn $E | awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /bl.*<(sys_rand_get|sys_csrand_get|z_impl_sys_rand_get)>/ {print fn, $0}' | sed 's/\t/ /g' | awk '{print $1, $NF}' | sort | uniq -c
