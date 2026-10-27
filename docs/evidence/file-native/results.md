# File native simulator verification

Initial Crux baseline `be0b252d78e1640bbebddefb4a6dd92fab563003` plus candidate
changes. Exact current source hashes and both native ELF/config/Devicetree/
executable/compile-command identities are in `manifest.json`. Native objects use
actual pinned Zephyr VFS and LittleFS; no target Flash driver is substituted.

- Final 32-bit and 64-bit native_sim executables each executed all seven cases:
  **14/14 PASS**, no skips. `native-32.log` and `native-64.log` are fresh manual
  executions after both final binaries were rebuilt, not inferred from build
  success. Twister's `--test-only` reused the previously passed profile status
  while running the previously failed one; both final executables were therefore
  explicitly rerun and checked. `twister.json` remains corroborating suite data.
- Private public-API-only C17 consumer was compiled, linked and run in each
  composition. Its actual compile commands expose no kernel headers, generated
  includes or `__ZEPHYR__` definitions. Width/non-PIE flags are toolchain ABI
  requirements, not native API requirements. ELF headers are native i386/x86-64;
  no ARM/XIP placement or physical safety follows from this ELF inspection.
- Independent Host C17/C++17 File consumers: **2/2 PASS**. Full final Host debug
  and ASan/UBSan regressions: **93/93 each PASS**. File cases include the existing
  actual Linux and deterministic providers plus the new sleepable lock case.
- Host File static analysis passed with no owned-code diagnostics. Retained
  output reports suppressed non-user/system warnings, not zero total warnings.
- Native-free/dependency entry point passed. Fifteen focused native-interface
  tests and two Configuration manifest-link safety regressions passed. An initial
  unittest run inherited GIT_DIR/GIT_WORK_TREE used to inspect the RO linked
  checkout; a temporary Git fixture failed setup. Unsetting those variables for
  the unittest runner corrected the environment; it was not a code failure.

## Test-first and review corrections

The initial fixture could not link without the provider; that preparation failure
is not counted as a behavioral test result. The existing shared/Host contract
suite was reused. A failing lock-admission trial was superseded after checking
consume-on-close: internal lock ownership failures must be fatal instead of
returning a close error with an unconsumed handle. Final tests check balanced
locking and a bounded isolated fatal native-acquisition failure.

Independent review identified real buffered-EOF and native64 count-narrowing
issues. The new seven-case native64 fixture produced **5 PASS / 2 FAIL** against
the initial provider: seek after an unflushed six-byte write was rejected, and
an oversized read was not rejected before dispatch. Minimal live-handle EOF and
signed32-limit corrections produce final **7/7 on both widths**. The beyond-EOF
case also verifies the original position through the literal subsequent `cde`
read. No production API or upstream source was changed.

Build-preparation failures were retained locally and excluded from passing
counts: deprecated partition macros, the shared 32-bit type-limit warning,
missing simulator callback Kconfig, an unnormalized module path, test-wrapper
link flags and native consumer ABI/PIC settings. No warning suppression was
introduced. Native boundary fault wrappers are applied to the final link, not
an ignored static-library PRIVATE link option.

The first full sanitizer run timed out in unchanged `Execution.HostFailures`
while emulated builds ran concurrently. The same test passed an isolated retry;
final full debug and sanitizer suites passed. The transient cause is unproven,
not declared a resolved product defect; both failure and retry logs are retained.

## Reproduction and limits

Use the commands in `docs/file-native-validation.md`, Linux host regression
commands and independent consumers from the existing test guide. The author used
pinned LittleFS archive SHA256 recorded in `manifest.json`; full generated builds
and the archive remain local. Standard CI does not execute this manual native
provider gate. Rerun it and affected review after source/configuration/pin changes.

Both simulator partitions are fixtures, not an adopted production layout. The
interruption checkpoint replays bytes captured before cleanup and proves only
its declared old-file outcome. It is not a physical reboot/power-cut test.
Formatting is explicitly test setup; provider code never formats. File-only
NO_FORMAT does not qualify all other system recovery writes. #14/#15/#16 and M4
remain gated; no hardware, production patch/layout, merge or Issue closure.
