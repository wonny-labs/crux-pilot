# Test foundation PoC runs — 3 November 2026

Evidence for the [test foundation proposal](../../test-foundation-proposal.md), [Issue #169](https://github.com/wonny-labs/crux-pilot/issues/169). **Software only. `native_sim` and Linux Host results. No hardware, flash, reset or debugger was used. This qualifies no board and adopts no interface.**

## Revisions and environment

- Crux: `ae7bdbc0a3c59191ad0db0a4c6922ba77a5a1af6`, on top of `origin/main` at `2760f59ef869687ca7f21eef8eb07f12a99a873d`. The commit is local only. The source was this commit's clean worktree, mounted read-only.
- Zephyr v4.4.0, `684c9e8f32e4373a21098559f748f06915f950c9`, the `west.yml` pin. Checkout clean.
- Image `crux-pilot-dev:latest`, `sha256:dbbf1293a2032554bd9174c44059859e3cf8bc4f1b05b30e2fb093858b8dbbd7`, linux/amd64 emulated on an Apple Silicon Mac, 2 CPUs and 4 GiB as in the Dev Container. Each run used a new throwaway container and the existing `crux-host-cache` volume.
- Python 3.12.3, pytest 8.3.5, CMake 3.28.4, west 1.4.0, GCC 13.3.0 for `native_sim` (`host_gnu`), Clang 18.1.3 for Host, Zephyr SDK 1.0.1 host tools.
- Build output was outside the Crux tree, under `/opt/crux-cache/test-foundation/`. `run_poc.sh` copies the fixture there first and refuses an output directory inside the Crux tree.

## Layouts

| Layout | Mounts | What it shows | Files |
| --- | --- | --- | --- |
| **Consumer workspace** | `/ws/zephyr` and a new `/ws/app/west.yml` that pins Zephyr and does not list Crux. Crux at `/opt/crux-src`. No HAL modules | `EXTRA_ZEPHYR_MODULES` is what supplies Crux: `zephyr_modules.txt` lists Crux only, and a build without the variable fails on undefined `CRUX_*` symbols | [consumer-workspace](consumer-workspace/) |
| **Dev Container** | The documented `/workspaces/crux` layout, with this worktree as `crux-pilot` | The same flow in the documented layout. West already finds Crux, so the variable is redundant: the same configure without it also succeeds | [dev-container-layout](dev-container-layout/) |

The consumer workspace manifest:

```yaml
manifest:
  projects:
    - name: zephyr
      url: https://github.com/zephyrproject-rtos/zephyr
      revision: 684c9e8f32e4373a21098559f748f06915f950c9
      west-commands: scripts/west-commands.yml
  self:
    path: app
```

## Commands

Consumer workspace, after `west init -l /ws/app` and with no `west update`:

```sh
ZEPHYR_BASE=/ws/zephyr POC_REQUIRE_EXTRA_MODULE=1 \
  /opt/crux-src/tests/consumer_poc/run_poc.sh /opt/crux-cache/test-foundation/run-consumer-ws
```

Dev Container layout, from `/workspaces/crux/crux-pilot`:

```sh
tests/consumer_poc/run_poc.sh /opt/crux-cache/test-foundation/run-devcontainer
```

`run_poc.sh` runs these. `$crux` is the Crux source, `$consumer` the copy outside it and `$out` the output directory:

```sh
cmake -S "$consumer" -B "$out/host" -G Ninja -DCRUX_SOURCE_DIR="$crux" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build "$out/host"
ctest --test-dir "$out/host" --output-on-failure -V --output-junit ctest-junit.xml
west twister -j 2 -p native_sim/native/64 -T "$consumer/zephyr_test" -O "$out/twister" --inline-logs \
  --extra-args=EXTRA_ZEPHYR_MODULES="$crux"
west build -p always -b native_sim/native/64 -d "$out/app" "$consumer/zephyr" -- -DEXTRA_ZEPHYR_MODULES="$crux"
PYTHONPATH="$ZEPHYR_BASE/scripts/pylib/pytest-twister-harness/src" python3 -m pytest -v -p no:cacheprovider \
  -p twister_harness.plugin --twister-harness --device-type=native --build-dir="$out/app" test_functional.py
# The same build and pytest commands with -DCONSUMER_DEFECT_REBASE=ON and "$out/app-defect".
```

## Results

Both layouts, at the revision above.

| Step | Level | Expected exit | Consumer workspace | Dev Container | Result |
| --- | --- | --- | --- | --- | --- |
| `host-unit-integration` | Unit and Host integration, CTest | 0 | 0, 17 s | 0, 20 s | `Consumer.Unit` and `Consumer.HostIntegration` pass, 2/2. On real Host scheduling the run reported `skipped=2`; the test asserts at least 2 |
| `twister-native-sim` | Ztest, `native_sim/native/64` | 0 | 0, 286 s | 0, 296 s | 1/1 configuration and case passed |
| `image-normal` | Build | 0 | 0, 130 s | 0, 105 s | Fresh Zephyr build |
| `functional-normal` | pytest, no Twister | 0 | 0, 1 s | 0, 1 s | 1 passed; case 0.32 s in the consumer workspace log |
| `image-defect` | Build | 0 | 0, 163 s | 0, 78 s | Fresh build with the defect |
| `functional-defect` | Controlled failure | **1** | 1, 1 s | 1, 0 s | 1 failed, 0 errors: `AssertionError: cycle 3: skipped 0, expected 2` |
| `defect-found-by-oracle` | Check of the above | 0 | 0 | 0 | JUnit shows `errors="0" failures="1"` and that message |
| `module-is-required` | Negative control | 0 | 0, 20 s | Not applicable | Build without the variable fails on undefined `CRUX_TIME` and `CRUX_THREAD` |

Times include emulation and are not performance figures; other containers were running.

Image SHA-256 values are in the `identity-*.json` files. They differ between the two layouts because source and output paths differ, and they are not a reproducibility claim.

The `defect-found-by-oracle` check earned its place during development: an early failed configure also made pytest exit 1, on a missing image and with `errors="1"`, and the check correctly rejected that run.

The test-first red step ran before the logic existed: `Consumer.Unit` failed 6 of its 9 checks against a stub. The output of that run was not retained.

## Probes

Kept in [probes](probes/). No west workspace; only `ZEPHYR_BASE` was set and Zephyr was mounted alone.

| Command | Exit | Finding |
| --- | --- | --- |
| `cmake -S .../consumer_poc/zephyr -B ... -DBOARD=native_sim/native/64 -DEXTRA_ZEPHYR_MODULES=/opt/crux-src` | 1 | [Log](probes/no-workspace-extra-modules.log). Missing `kconfig_module_dirs.cmake`, undefined `CRUX_*` symbols. Reproduced; the cause is the `if(WEST OR ZEPHYR_MODULES)` test in `zephyr_module.cmake` |
| The same with `-DZEPHYR_MODULES=/opt/crux-src` | 0 | [Log](probes/no-workspace-zephyr-modules.log). Configure only, not built |
| Import of `twister_harness.plugin` and `twister_harness.fixtures` | 0 | [Modules loaded](probes/harness-imports.txt). Four Zephyr files; `yaml` and `pykwalify` load but `setup.cfg` does not declare them |
| The same import with `ZEPHYR_BASE` unset | Failed | `OSError: ZEPHYR_BASE environment variable is not set` |

In the Dev Container layout, `west build -b native_sim/native/64 -d ... crux-pilot/tests/consumer_poc/zephyr --cmake-only` with no module variable exited 0 and listed Crux once. The output of this probe was not retained.

## Other checks on the fixture

- `clang-format --dry-run --Werror` on the fixture's C files: pass, with the repository's `.clang-format`.
- `scripts/check-native-interfaces.py --root`: pass. It does not scan this fixture; `crux_assert_portable_consumer(consumer_app)` is what checks the boundary, and it passed at configure.
- `python3 scripts/check_docs.py` with the proposal and this record staged: pass, exit 0, 0 link errors. Offline; external URLs are not verified.

## Not run

- **Any macOS or Windows execution.** The portability table in the proposal is source reading only.
- **Any hardware path:** `--device-type=hardware`, serial, `west flash`, `--flash-command`, `--pre-script`, reset. Hardware was excluded from this work.
- `dut.write` or the harness `shell` fixture. The PoC's application only prints. The device-control `write` operation is therefore unexercised.
- The QEMU adapter, and Twister `harness: pytest`, which [#60](https://github.com/wonny-labs/crux-pilot/issues/60) already covered.
- The device-control contract and hand-off manifest: proposed, with nothing implemented.
- The repository's Host, sanitizer and Zephyr suites. No product code, public API or existing suite changed. CI did not run; the branch is local.
- An application that lists Crux in its own `west.yml` projects, which is the proposal's packaging route. The consumer workspace used a read-only mount and `EXTRA_ZEPHYR_MODULES`.
