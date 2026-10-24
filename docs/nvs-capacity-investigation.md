# Candidate NVS capacity, CRC and remount investigation

## Scope and composition

This software investigation implements the user-approved capacity/CRC/remount
part of the existing actual-NVS-engine plan. Baseline Crux main is
`ca7655b9ebce6debe444456eb0c4197a0f76f838`; Zephyr remains
`684c9e8f32e4373a21098559f748f06915f950c9` (4.4.0).
The actual Settings store/NVS backend, NVS engine and simulated Flash are built
without the experimental patches from PR #44 or PR #45. This is not a native
Configuration provider or permission to adopt a production backend/layout.

The test-only chosen partition is `[0x100000,0x110000)` on the 2 MiB native_sim
Flash device: **65536 raw bytes, sixteen 4096-byte sectors, 1-byte writes**.
Its address differs from the proposed target allocation intentionally. It follows
the simulator's original storage area without overlap. Both Devicetree and actual
runtime NVS sector count/size are checked, preventing a misleading test of the
Settings default eight-sector clipping rather than the candidate whole region.

Two independently initialized profiles use data CRC off/on. NVS lookup cache has
256 entries; the separate Settings name cache and bad-region reinitialization are
off. These are explicit software test configurations, not adopted controller
configuration or a CRC migration test. Disabling bad-region reinitialization does
not make mount read-only or prevent every recovery erase; PR #45 remains unadopted.

## Workloads and assertions

- **Canonical records:** a test-only generator emits the approved version1,
  schema1 BOOL/U32 byte encoding, 5/8 bytes respectively, alternating by index.
  Four literal golden vectors are checked with strict C17. The generator's
  implementation was added only after the test failed for the missing function.
  Native tests exercise existing engine behavior; no native bugfix RED/GREEN
  claim is made.
- **Mixed roundtrip:** 64 real Settings names, initial writes and three update
  rounds. Every acknowledged value is checked before and after each of four
  backend remounts; an unchanged save is also accepted.
- **Capacity:** distinct names `cfg/p%04u` (9 bytes, excluding NUL), alternating
  BOOL/U32 records, until the first failing new-key save, bounded at 4000 attempts.
  The observed result and acknowledged payload bytes are recorded separately from
  raw allocation. Real Settings name/count records, NVS metadata/CRC, reserved
  space and garbage collection are included. No theoretical fill percentage is
  substituted for a measured result.
- **Reclamation:** after post-ENOSPC remount, verify all acknowledged keys; delete
  the first half, add 16 new keys, remount and verify all survivors/new values.
  Skip the failed new-key index. Count/orphan-record changes can remain after a
  failed save; this is not rollback or a transaction. The checker rejects
  additional named records in this particular no-fault ENOSPC run, not in every
  possible native failure scenario.
- **CRC:** write a U32 through Settings; locate its unique payload and flip one
  byte through the simulator's explicit memory API. Compare full and one-byte
  native reads and an actual Settings full-read callback, before/after remount.
  This models payload corruption, not a failed Flash programming operation.
  The CRC-on case explicitly asserts outer Settings `0` alongside callback
  `-EIO`, before and after remount. This characterizes a known limitation of the
  pinned baseline, not accepted Configuration provider behavior.

Each case starts by erasing only the owned simulator area. Remount discards the
NVS/count state and reconstructs it from retained simulated bytes, keeping the
registered Settings store address and Flash device alive. No operation is in
flight and the name cache is off. This does not restart the process, reset all
Settings globals, operate the board or establish physical reboot/power-loss safety.

## Reproduction and evidence gate

Use the existing Linux west workspace, pinned source/toolchain and native_sim
32-bit build dependencies. From the repository root:

```sh
cc -std=c17 -Wall -Wextra -Werror \
  tests/native/nvs_capacity/test_records.c tests/native/nvs_capacity/records.c \
  -o /tmp/nvs-records-test
/tmp/nvs-records-test

export ZEPHYR_BASE=/path/to/workspace/zephyr
export ZEPHYR_TOOLCHAIN_VARIANT=host
python3 "$ZEPHYR_BASE/scripts/twister" -T tests/native/nvs_capacity \
  -p native_sim -O /tmp/nvs-capacity -j2 --inline-logs
```

The native fixture is restricted to native_sim and simulated Flash. No source
copy/patch hook or board programming/erase tool is selected. Native mutation is
limited to simulator memory. Retain generated `.config`, Devicetree, build source
commands, Twister report, handler logs and their hashes. Existing Host/Zephyr CI
is regression evidence only: it does not execute this manual investigation gate.
Formatting and dependency guards passed. The manual gate and affected independent
review are repeated after the outer-result assertion change; retained source hashes
connect each run to its tested inputs. Rerun the fixture and independent review
after changing its source, pin or profile.

## Measured results

Both executed configurations passed: **6/6 native cases**, no filters/skips,
build errors or warnings. The four independent C17 golden vectors also passed.
The saturation loop was bounded to 4000 attempts (within a 4096-entry test map);
it stopped on `-ENOSPC` in both profiles.

| Fresh profile | Acknowledged before first ENOSPC | Canonical payload bytes | Deleted | New writes after deletion | Verified after reclamation/remount |
| --- | ---: | ---: | ---: | ---: | ---: |
| Data CRC off | 1939 | 12602 | 969 | 16 | 986 |
| Data CRC on | 1545 | 10041 | 772 | 16 | 789 |

All acknowledged keys were verified after the post-ENOSPC remount. No additional
named key was observed in this run; this does not establish rollback of the
failed attempt's count/orphan data. Each profile also verified 64 mixed keys over
three update rounds and four remounts. These are successful operation/readback
observations, not physical durable-write acknowledgments.

For deliberate U32 corruption, results were unchanged before/after remount:

| Profile | Native full read | Native one-byte read | Settings full-read callback | Outer Settings result |
| --- | --- | --- | --- | --- |
| CRC off | 8, corrupted payload copied | 8 advertised | 8, corrupted payload copied | 0 |
| CRC on | `-EIO` | 8 advertised | `-EIO` | 0 |

CRC-enabled NVS remains the preferred design candidate. The CRC-off profile is a
diagnostic counterexample, not a recommended setting.
Expected native CRC-error log lines in the CRC-on case are part of the test.
Ztest's `0.000 seconds` durations use the simulated clock and provide no latency
or throughput evidence. Final 2 MiB simulator images and extracted 64 KiB regions
are archived locally with hashes, after the final mixed-roundtrip case. They are
not snapshots of every capacity/corruption phase. Full generated builds/reports
are also retained locally; the repository keeps compact logs/manifests.

## Interpretation and remaining gates

Read [retained results](evidence/nvs-capacity/results.json) and the adjacent logs
for actual counts, source/config/build hashes and native statuses. See the
[independent review record](evidence/nvs-capacity/review.md) for its scope/limits.

Capacity applies to the specified name length, type mix, write order, update and
reclamation history. Longer namespace/keys, a different live set or cached/backend
configuration can change it. The application maximum parameter count/name budget
and safety headroom must be declared before approving production capacity; these
results alone do not establish that 64 KiB is sufficient for every consumer.

Data CRC is checked by NVS only for a full payload read. A partial read can return
the full advertised length without checking the CRC. Settings wrapper success
must not be treated as proof that its read callback succeeded; the fixture
observes both channels independently. A failing full read may already have copied
unvalidated bytes into the buffer, so those bytes cannot be accepted as active
Configuration values. The native provider remains unimplemented and gated.

No source remediation, backend/layout adoption, actual persistence provider,
CRC compatibility migration, target XIP closure, image/programmer erase fencing,
endurance/timing guarantee or physical qualification is delivered. PR #44/#45
error-path candidates remain unadopted. #14 still needs recorded design/capacity
policy decisions; #15/#16 retain native error/synchronization, storage ownership,
qualified recovery and separate physical approval/acceptance requirements.
