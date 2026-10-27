#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
for profile in baseline enabled; do
    extra=()
    if [[ "$profile" == enabled ]]; then
        extra+=("-DEXTRA_CONF_FILE=$repo/tests/messaging_resources/enabled.conf")
    fi
    (cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
        -d "$repo/build/zephyr/messaging-resources-$profile" "$repo/tests/messaging_resources" -- \
        -DZEPHYR_MODULES="$modules_text" "${extra[@]}")
done
# Static image comparison only. Never flash, program, reset or access a board.

# Retain reproducible static accounting alongside the two build directories.
# Resolve the selected SDK from the environment, or the generated build cache.
evidence="$repo/build/messaging-resource-evidence"
mkdir -p "$evidence"
sdk="${ZEPHYR_SDK_INSTALL_DIR:-$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:INTERNAL=//p' \
    "$repo/build/zephyr/messaging-resources-enabled/CMakeCache.txt")}"
tool_bin="$sdk/gnu/arm-zephyr-eabi/bin"
if [[ ! -x "$tool_bin/arm-zephyr-eabi-size" ]]; then
    tool_bin="$sdk/arm-zephyr-eabi/bin"
fi
cross="$tool_bin/arm-zephyr-eabi"
"$cross-gcc" --version > "$evidence/compiler.txt"
git -C "$repo" rev-parse HEAD > "$evidence/base-revision.txt"
(cd "$workspace" && west list zephyr cmsis cmsis_6 hal_nxp -f '{name} {revision} {path}') \
    > "$evidence/modules.txt"
(cd "$repo" && git ls-files --cached --others --exclude-standard \
    components/queue components/time components/thread zephyr cmake/CruxConsumerBoundary.cmake \
    tests/messaging_resources scripts/measure-messaging.sh | sort -u | \
    while IFS= read -r file; do sha256sum "$file"; done) > "$evidence/input-sha256.txt"
: > "$evidence/commands.txt"
: > "$evidence/elf-sha256.txt"
for profile in baseline enabled; do
    build="$repo/build/zephyr/messaging-resources-$profile"
    image="$build/zephyr/zephyr.elf"
    printf '%s\n' \
        "$cross-size $image" "$cross-size -A $image" \
        "$cross-nm -S --size-sort --radix=d $image" \
        "$cross-objdump -d --disassemble=main $image" \
        "cmake --build $build --target rom_report ram_report" \
        >> "$evidence/commands.txt"
    "$cross-size" "$image" > "$evidence/$profile-size.txt"
    "$cross-size" -A "$image" > "$evidence/$profile-sections.txt"
    "$cross-nm" -S --size-sort --radix=d "$image" > "$evidence/$profile-nm.txt"
    "$cross-objdump" -d --disassemble=main "$image" > "$evidence/$profile-main.txt"
    cmake --build "$build" --target rom_report ram_report > "$evidence/$profile-reports.txt" 2>&1
    cp "$build/zephyr/.config" "$evidence/$profile.config"
    sha256sum "$image" >> "$evidence/elf-sha256.txt"
done
for api in crux_queue_create crux_queue_send crux_queue_receive crux_queue_destroy \
    crux_monotonic_now crux_thread_sleep_until; do
    if ! awk -v symbol="$api" '$3 == "T" && $4 == symbol {found=1} END {exit !found}' \
        "$evidence/enabled-nm.txt"; then
        printf 'Required retained API missing: %s\n' "$api" >&2
        exit 1
    fi
done
diff -u "$evidence/baseline.config" "$evidence/enabled.config" > "$evidence/config.diff" \
    || test "$?" -eq 1
# Remove machine-specific roots from textual evidence; original ELFs remain in build/zephyr.
python3 - "$evidence" "$repo" "$workspace" "$sdk" <<'PY_NORMALIZE'
from pathlib import Path
import sys
root = Path(sys.argv[1])
replacements = list(zip(sys.argv[2:], ("<REPO>", "<WORKSPACE>", "<SDK>")))
replacements.append((sys.executable, "<PYTHON>"))
replacements.sort(key=lambda pair: len(pair[0]), reverse=True)
for path in root.iterdir():
    if path.is_file():
        text = path.read_text()
        for original, replacement in replacements:
            if original:
                text = text.replace(original, replacement)
        path.write_text(text)
PY_NORMALIZE
printf 'Static resource evidence: %s\n' "$evidence"
