SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=/build/bench_c1/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size -A $E > /build/bench_c1.size-sections.txt
$SDK/arm-zephyr-eabi-readelf -lW $E > /build/bench_c1.segments.txt
$SDK/arm-zephyr-eabi-nm -S --size-sort -r $E | head -400 > /build/bench_c1.nm-largest-400.txt
