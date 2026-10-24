# Settings readiness gate: software candidate

This is an isolated native_sim composition experiment built from PR #59
(`f6f539dc7d64f4a11a669dc84648aaaba276c8d3`). It does not adopt a production
provider, storage layout, recovery policy or public interface. Existing sources,
prior evidence and the pinned Zephyr checkout remain unchanged.

PR #59's narrowed copies propagate selected Settings/NVS failures, refuse an
absent or malformed count, and avoid orphan repair during traversal. Two gaps
remain without this experiment: NVS mount can mutate before Settings validates
the count; save preflight probes only one value byte, which does not validate a
full NVS data CRC. A corrupted current or other claimed key can consequently be
overwritten by a save.

## Private composition

[`settings_readiness_gate`](../tests/native/settings_readiness_gate/) compiles the
actual Configuration implementation and existing Settings provider with the same
pinned source-copy preparation and patches as
[`settings_target_candidate/provider`](../tests/native/settings_target_candidate/provider/).
It adds no upstream patch, independent media scanner or reusable service API.
GNU link wrappers are confined to this executable.

`flash_area_open` returns a private area/device proxy only for the chosen Settings
partition. It is installed before `settings_subsys_init` reaches `nvs_mount`.
Other areas keep their original device. The selected NVS mutation paths use
`flash_write` and `flash_flatten`; the pinned Flash utility dispatches flatten
through the device's erase operation (or fill/write operation). Both operations
therefore reach this proxy. This is a source-path statement about this selected
composition, not an interlock for all Flash clients, extended operations or NOR
boot/status commands.

The gate begins **CLOSED**. Reads are allowed, while a write or erase outside
**READY** is denied before simulator-driver dispatch and latches **FAULTED**.
Every original Flash read/write/erase error also latches failure. After successful
backend initialization, **SCANNING** traverses the Settings backend's claimed
live records with writes still denied. Each record must have a nonempty single
key under `cfg/`, a value length from 1 through 256 bytes, and a full successful
value read. Negative and short callback results are errors. Full reads exercise
NVS data CRC validation; a one-byte presence probe is insufficient. No scan data
is published to a Configuration parameter.

Only a completed scan with no error or latched denied mutation enables **READY**.
Before each public Configuration save, the wrapper closes write permission and
repeats the same complete claimed-record traversal. This prevents a corrupt
other key from being overwritten while saving the requested key. The caller must
serialize initialization, loads and saves and own this Settings store exclusively;
there is no concurrency or competing-client qualification.

A fault refuses subsequent public Configuration load/save calls with
`CRUX_CONFIG_ERR_NOT_READY`, preserving the active value. The first failing call
retains the existing provider's declared error result; an apparent success or
missing result cannot override a latched lower Flash fault. Tests mask the
return from actual Settings initialization, traversal and save to establish that
this does not depend on the inner layer returning its error correctly. Clearing
an injected fault alone cannot reopen the gate. Explicit test reinitialization
resets both the private gate and Settings initialization state; no process or
hardware reset is used.

Provisioning is a separate explicit test setup: direct simulator erase, NVS
initialization and writing the known empty Settings count. Runtime startup never
falls back to formatting or provisioning. Erased media is refused, with its
attempted GC-marker write denied before dispatch. A dirty next sector exercises
a real startup erase attempt, also denied before dispatch.

## Evidence and reproduction

The suite covers healthy BOOL/U32 round trips, missing-key defaults on a known
provisioned store, permitted writes, fresh refusal, mount repair denial, count
and orphan failures, corrupt current/other-key save refusal, corrupt startup,
hard reads, oversize and foreign namespace refusal, sticky faults, masked inner
errors, explicit clean-media reinitialization and preservation of the provider's
no-space result. A CRC-valid malformed canonical record still follows actual
Configuration policy: `CRUX_CONFIG_DEFAULT_REJECTED` and the declared default.
Transport readiness does not mean every application's parameter is valid.

The final identical 18-case suite reports **4 pass / 14 fail** with the gate
removed and **18 pass / 0 fail** with the gate enabled. In the red run, corrupt
current-key and other-key saves return success and dispatch 3 and 9 Flash writes
respectively. Both fresh and dirty-next-sector startup change the selected sector.
The green run refuses these operations, preserves the compared 4096-byte sectors
and records zero dispatched write/erase operations in each refusal case. A
focused intermediate run also caught a no-space-to-I/O remapping regression
before the final result-preserving wrapper change.

Run inside the existing Linux west workspace, with dependencies already present:

```sh
export PATH=/opt/crux-cache/venv/bin:$PATH
export ZEPHYR_TOOLCHAIN_VARIANT=host
cd /ws
west build -b native_sim/native/64 \
  /ws/.worktrees/flash-safety-poc/tests/native/settings_readiness_gate \
  -d /tmp/settings-readiness-gate/green
cd /tmp/settings-readiness-gate/green
./zephyr/zephyr.exe
```

Run from the writable output directory because Flashsim uses a local backing
file. Build the identical tests with `-DCRUX_READINESS_GATE=OFF` in a separate
output directory to select the retained dispatch-only red baseline. Both builds
retain the PR #59 narrowed Settings/NVS copies. Compiler output and full
executables remain outside the repository; compact logs, selected configuration,
source hashes and the red baseline source are under
[`evidence`](../tests/native/settings_readiness_gate/evidence/). The no-space
injection is one deterministic Flash-boundary error, not a capacity sweep.

## Limits and next decision

This establishes bounded full-record **transport/CRC preflight over the backend's
claimed live records**, not whole-media integrity, application-wide validation or
production readiness. It does not qualify stale counts, unclaimed records,
duplicate IDs, NVS classification/recovery, crash consistency, power loss or every
startup/GC state. Schema, canonical encoding and application validators remain
Configuration load policy. A CRC-valid malformed record need not fault transport
readiness; it must never become an unvalidated active value.

The gate permits normal backend writes after readiness, including any GC reached
by those writes, but this suite does not qualify general GC or capacity behavior.
The short-read rejection branch is explicit; the retained native runs inject
hard read and full-record CRC failures rather than an artificial short Settings
callback. These are simulated software results with no physical Flash, watchdog,
exception/XIP, target FlexSPI behavior or persistent-board evidence. Production
composition, first provisioning, recovery and Flash coordination remain separate
human decisions. No merge, Issue closure or hardware operation follows from a
passing test.
