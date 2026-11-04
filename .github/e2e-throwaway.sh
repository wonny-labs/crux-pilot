#!/usr/bin/env bash
# Throwaway. Follows the guide's release flow through the template's own manifest.
set -uo pipefail
banner() { printf '\n========== %s ==========\n' "$*"; }
SOURCE="$GITHUB_WORKSPACE/crux-pilot"
BOARD="$CRUX_BOARD_TARGET"
ROOT="$RUNNER_TEMP/e2e"
BARE="$ROOT/product-e2e.git"
OUT="$ROOT/out"
mkdir -p "$ROOT" "$OUT"
export WEST_CONFIG_GLOBAL="$ROOT/westconfig"
git config --global user.name e2e
git config --global user.email e2e@example.invalid
git config --global advice.detachedHead false

banner 'E1 product from the template'
git init -q --bare "$BARE"; git -C "$BARE" symbolic-ref HEAD refs/heads/main
git clone -q "$BARE" "$ROOT/work" 2>/dev/null
cd "$ROOT/work"; git checkout -q -b main 2>/dev/null
cp -R "$SOURCE/samples/product_template/." .
sed -i -E "s|^( +)revision: main.*|\1revision: $CRUX_HEAD_SHA|" west.yml
grep -n -E '^ +revision:|^ +url-base:|name: crux-pilot' west.yml
git add -A; git commit -q -m 'product from the template'; git push -q origin main

banner 'E2 freeze'
Z="$ROOT/freeze"
west init -m "file://$BARE" --mr main "$Z" 2>&1 | tail -1
cd "$Z"
west update > "$OUT/freeze-update.log" 2>&1; echo "update exit=$?"
west crux-pins; echo "crux-pins exit=$?"
mkdir -p product-x/locks
west manifest --freeze --active-only > product-x/locks/west.frozen.yml; echo "freeze exit=$?"
cd product-x
git checkout -q -b release/1.0; git add -A; git commit -q -m 'release 1.0: lock'
git tag -a product-e2e-1.0.0 -m release; git push -q origin release/1.0 product-e2e-1.0.0
cd "$ROOT"; rm -rf "$Z"

release() { # workspace, label
    local ws=$1 label=$2 rel=product-x/build/rel ver=product-x/build/v
    banner "E3 $label at $ws"
    mkdir -p "$(dirname "$ws")"
    west init -m "file://$BARE" --mr product-e2e-1.0.0 --mf locks/west.frozen.yml "$ws" 2>&1 | tail -1
    cd "$ws"
    west update > "$OUT/$label-update.log" 2>&1; echo "update exit=$?"
    west crux-pins; echo "crux-pins exit=$?"
    local record=(python3 crux-pilot/scripts/record-build-provenance.py --repo product-x --workspace "$ws")
    "${record[@]}" --capture-inputs "$OUT/$label-in.json" | tail -1; echo "capture exit=${PIPESTATUS[0]}"
    west spdx --init -d "$rel" > /dev/null 2>&1
    west build -b "$BOARD" -d "$rel" product-x/app -- -DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON > "$OUT/$label-build.log" 2>&1
    echo "build with required gate exit=$?"; grep -c 'verified on Zephyr' "$OUT/$label-build.log"
    for t in rom_report ram_report; do west build -d "$rel" -t $t > /dev/null 2>&1; echo "$t exit=$?"; done
    west spdx -d "$rel" 2>&1 | tail -1
    "${record[@]}" --build-dir "$rel" --target "$BOARD" --inputs "$OUT/$label-in.json" | grep -E 'Build identity|Firmware identity|recoverable' ; echo "record exit=${PIPESTATUS[0]}"
    python3 - "$rel/provenance.json" "$ws" "$OUT/$label.txt" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("firmware_identity", json.dumps(d["firmware_identity"], sort_keys=True))
print("recipe source", d.get("devcontainer_recipe_source"))
print("dirty", sorted({p["dirty"] for p in d["west_projects"]}), "projects", len(d["west_projects"]))
print("recoverable", d["source"]["recoverable_from_git"], "repository", d["repository"])
print("workspace path in firmware_identity:", sys.argv[2] in json.dumps(d["firmware_identity"]))
open(sys.argv[3], "w").write(json.dumps(d["firmware_identity"], sort_keys=True) + "\n" + d["build_id"] + "\n")
PY
    sha256sum "$rel/zephyr/zephyr.bin" | cut -c1-64 | tee "$OUT/$label.bin"
    printf 'whole config %s\n' "$(sha256sum "$rel/zephyr/.config" | cut -c1-16)"
    printf 'elf %s\n' "$(sha256sum "$rel/zephyr/zephyr.elf" | cut -c1-16)"
    grep -a -o -E 'Booting Zephyr OS[ -~]{0,50}' "$rel/zephyr/zephyr.bin" | head -1
    grep -n 'external\|ExternalRef' "$rel/spdx/zephyr.spdx" | head -3 | cut -c1-110
    banner "N3 $label BUILD_VERSION alone"
    west build -b "$BOARD" -d "$ver" product-x/app -- -DBUILD_VERSION=product-x-1.0.0 > "$OUT/$label-v.log" 2>&1; echo "build exit=$?"
    sha256sum "$ver/zephyr/zephyr.bin" | cut -c1-64 | tee "$OUT/$label.ver"
    cd "$ROOT"; rm -rf "$ws"
}
release "$ROOT/a/ws" A
release "$ROOT/elsewhere/much/deeper/ws" B

banner 'E4 another Zephyr'
G="$ROOT/gate"
west init -m "file://$BARE" --mr main "$G" 2>&1 | tail -1
cd "$G/product-x"
python3 - <<'PY'
from pathlib import Path
p = Path("west.yml"); t = p.read_text()
entry = ("    - name: zephyr\n      url: https://github.com/zephyrproject-rtos/zephyr\n      revision: v4.4.0\n"
         "      import:\n        name-allowlist: [cmsis, cmsis_6, hal_nxp, littlefs, fatfs]\n")
assert "  projects:\n" in t
p.write_text(t.replace("  projects:\n", "  projects:\n" + entry, 1))
PY
git add -A; git commit -q -m 'pick another zephyr'
cd "$G"
west update > "$OUT/gate-update.log" 2>&1; echo "update exit=$?"
west crux-pins; echo "crux-pins exit=$? (expected non-zero)"
west build --cmake-only -b "$BOARD" -d product-x/build/w product-x/app > "$OUT/w.log" 2>&1; echo "default configure exit=$? (expected 0)"
grep -n 'verified on Zephyr' "$OUT/w.log" | head -2 | cut -c1-180
west build --cmake-only -b "$BOARD" -d product-x/build/x product-x/app -- -DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON > "$OUT/x.log" 2>&1; echo "required configure exit=$? (expected non-zero)"
grep -n 'verified on Zephyr' "$OUT/x.log" | head -2 | cut -c1-180
west build --cmake-only -b "$BOARD" -d product-x/build/off product-x/app -- -DCONFIG_CRUX_PILOT_MODULE=n > "$OUT/off.log" 2>&1; echo "module off configure exit=$?"
grep -c 'verified on Zephyr' "$OUT/off.log"
cd "$ROOT"; rm -rf "$G"

banner 'Compare'
for f in A B; do echo "$f firmware: $(sed -n 1p "$OUT/$f.txt" | sha256sum | cut -c1-16)  build_id: $(sed -n 2p "$OUT/$f.txt" | cut -c1-16)  bin: $(cut -c1-16 "$OUT/$f.bin")  version build: $(cut -c1-16 "$OUT/$f.ver")"; done
status=0
cmp -s <(sed -n 1p "$OUT/A.txt") <(sed -n 1p "$OUT/B.txt") && echo 'firmware_identity identical' || { echo 'firmware_identity DIFFERS'; status=1; }
cmp -s "$OUT/A.bin" "$OUT/B.bin" && echo 'zephyr.bin identical' || { echo 'zephyr.bin DIFFERS'; status=1; }
cmp -s <(sed -n 2p "$OUT/A.txt") <(sed -n 2p "$OUT/B.txt") && echo 'build_id identical' || echo 'build_id differs'
cmp -s "$OUT/A.ver" "$OUT/B.ver" && echo 'BUILD_VERSION build identical' || echo 'BUILD_VERSION build differs'
exit "$status"
