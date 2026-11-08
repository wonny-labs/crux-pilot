SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=/build/bench_c1b/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size -A $E > /build/bench_c1b.size-sections.txt
$SDK/arm-zephyr-eabi-readelf -lW $E > /build/bench_c1b.segments.txt
$SDK/arm-zephyr-eabi-nm -S --size-sort -r $E | head -400 > /build/bench_c1b.nm-largest-400.txt
$SDK/arm-zephyr-eabi-nm -S $E | grep -iE "http_server|_stack|thread_stack" | sort -k2 > /build/bench_c1b.stacks.txt
