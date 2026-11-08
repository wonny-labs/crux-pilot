set -e
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
N=$1; E=/build/evidence/$N; ELF=/build/$N/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size -A $ELF > $E/size-sections.txt
$SDK/arm-zephyr-eabi-nm -S --size-sort -r -C $ELF > $E/nm-by-size.txt
$SDK/arm-zephyr-eabi-readelf -S -W $ELF > $E/sections.txt
$SDK/arm-zephyr-eabi-readelf -l -W $ELF > $E/segments.txt
