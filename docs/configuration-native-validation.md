# Simulator Configuration provider candidate

This D3 candidate composes the unchanged portable Configuration implementation
with Settings, NVS and simulated Flash. It is not production adoption, a target
layout, physical reboot evidence, or permission for target writes. The private
provider is explicitly initialized in caller-serialized task context. Applications
still own validation and runtime activation. Settings namespace `cfg/` is private
to this composition. The actual NVS store handles persistence cases. One additional test-only Settings
source supplies a callback short count for its unique `cfg/short` key; NVS itself
returns advertised length or an errno and cannot supply that malformed callback
result. This source is inactive for all other test cases.

The fixture replaces exactly three upstream source registrations with disposable
copies at configure time. `prepare.py` verifies the pinned revision and source
identity, then reuses the #44 Settings store/backend error patches and #45 NVS
initial-scan correction, plus a separate D3 correction for the later all-open-sector
comparison. The original #45 patch is unchanged. No pinned checkout source is changed. The generated
`configuration-candidate/manifest.json` identifies the inputs and patched copies.
These remain experimental patches with their original review/adoption limits.

Only `native_sim` with simulated Flash, Settings NVS, data CRC, disabled name
cache and disabled bad-region reinitialization is supported. Both the fixture
and provider reject other builds. The module's production Kconfig/CMake does
not select this provider. The isolated fixture explicitly selects the portable
source, private provider and private initialization header. No public API changes
are required, and no hardware provider wiring is proposed by this candidate.

Reads stage a full record (bounded at 256 bytes) and require the callback return
to match its advertised length, ensuring NVS checks the full data CRC. The
provider also checks callback-local errors and the outer traversal result before
publishing bytes or declaring absence. Oversized records beyond the staging bound
return IO with the active value preserved; they are not treated as missing or
validated malformed records. Complete malformed encodings within the bound flow
to the existing portable rejection/default rules. Errors after any callback still
prevent publication. Errors do not imply rollback of stored bytes.

The test Flash proxy sits beneath the actual NVS engine and forwards healthy
operations to the actual simulated Flash driver. Read and write faults are
explicit deterministic API failures. One interrupted-save case allows payload
write and fails its ATE write, discards backend RAM state, remounts, and verifies
the previous acknowledged value before a successful retry. This is a bounded
software interruption model, not a physical power-cut or process-reboot test.

Validation results and exact commands are recorded in
[evidence/configuration-native](evidence/configuration-native/). No capacity replay,
physical board tests, target XIP claims, multi-parameter atomicity, production
recovery policy, wear/endurance or universal failure guarantee is claimed.

## Remaining destructive recovery gate

Settings traversal is not universally read-only: the #44 candidate retains repair
of confirmed missing name/value records, including deletes and count updates, and
propagates repair failures. Its behavior is distinct from hard-read failure, which
must remain observable. Disabling `CONFIG_NVS_INIT_BAD_MEMORY_REGION` prevents the
specific all-sectors-closed reinitialization path; it does not disable other NVS
mount repair, metadata writes, interrupted-GC recovery or sector erasure. Empty
mount itself writes a GC-done marker. The narrowly tested read-failure cases stop
before further activity at those comparisons; they do not establish universally
safe or non-destructive startup. An uncertain or destructive recovery policy is
still a D4 adoption blocker, and requires a separately reviewed final storage
configuration and protection/safety argument before target use.

The additional D3 late-mount regression covers the reachable comparison immediately
before the last closing ATE when all sectors are open. With only the original #45
patch, a one-shot failure there was overwritten by recovery: mount returned success
after 786 reads and one write. The additional copied-source correction preserves
EIO after 17 reads with zero writes/erases and `ready == false`. This is a scoped
error-propagation correction, not a complete NVS startup safety qualification.
