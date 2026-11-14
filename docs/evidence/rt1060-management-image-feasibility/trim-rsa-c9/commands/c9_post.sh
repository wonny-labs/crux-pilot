# usage: c9_post.sh : static stack chains by address for both ELFs kept by c9_build.sh (host objdump of the arm toolchain)
set -u
export PATH=/opt/crux-cache/venv/bin:$PATH
export PYTHONDONTWRITEBYTECODE=1
for n in base after; do
  python3 /ws/crux-pilot/docs/evidence/rt1060-management-image-feasibility/trim-rsa-c9/commands/callgraph_by_address.py /build/keep/$n/zephyr.elf /build/keep/$n/roots.txt /build/keep/$n/static-stack-chains-by-address.json > /build/keep/$n/static-stack-chains-by-address.txt
done
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
for n in base after; do
  $SDK/arm-zephyr-eabi-readelf -S -W /build/keep/$n/zephyr.elf > /build/keep/$n/sections.txt
done
echo post-done
