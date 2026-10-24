# Isolated RT1060 storage software integration

**Software candidate only. Do not program or boot this image.** This change connects the existing File and Configuration providers to the exact experimental EVKB-B/QSPI sample. It does not adopt a production provider, upstream patch or partition layout. M4 and physical persistence remain incomplete.

## Implemented boundary

The ordinary simulator restrictions/defaults remain. The private application and board selection permits the existing providers in this sample; native_sim width/non-PIE flags are excluded from ARM consumers. Four independent public-header C17/C++17 consumer objects compile with the ARM ABI and native-free usage checks. This is compilation evidence, not execution of those consumers on Cortex-M.

Three owner-bound, deferred Flash API proxies reuse the bounded RAM command interface. Full owner ranges are checked before chunking; reads/programs are at most 256 bytes and erases are 4 KiB. The first error stops further commands and closes admission; earlier chunks may have completed. Only a completed, known-ready READ publishes exact-length results, once. The actual NOR/FlexSPI devices are deferred as well: proxy initialization and backend access require private maintenance ownership and command prerequisites.

The fixed trial uses existing portable APIs, controlled Settings/NVS corrections and the full-record readiness gate. It leaves scratch untouched and covers a file write/sync/consuming-close/read, two BOOL/U32 keys/four saves and healthy remount readback. Explicit provisioning is a separate destructive choice; mount/read errors never trigger formatting. Unknown storage fails closed. Existing representative normal GC evidence remains applicable. The File media fence remains effective during uncertain cleanup close.

`main` remains inert. Actual command submission and the private prerequisite return `-EACCES`; the outer elapsed qualification remains constant refusal. There is no target-enable option. The trial returns success only after unmount, retired handles and one-way maintenance/native exit. Later File access and Configuration persistence are refused; RAM Configuration operations remain available. The standalone sample has no controller-start wiring. That integration is deferred, rather than represented as tested startup.

## Verified evidence

Tested implementation: `18182984c99c7ccadc660ecb8ba0f237e9c0732e`, stacked on PR #63 at `a9928a5`.

| Gate | Result |
| --- | --- |
| Focused selection, adapter/read-result and exact-image checks | 12 methods PASS |
| Affected command/regression/image checks | 33 methods PASS |
| Existing real-engine maintenance, including shared fixed trial | 10 cases PASS |
| Standalone LittleFS provider / Settings readiness | 7 / 18 cases PASS |
| Full real-engine composition | Positive, refused-prerequisite and unprovisioned scenarios PASS |
| ARM image, init priorities, image bounds and independent consumer ABI | PASS |
| Native-free dependency/public-provider checks | PASS, 50 designated portable sources |

No final skips. These totals include positive and negative cases. The positive composition uses simulated Flash as its controlled command leaf; separate tests exercise the actual staged RAM dispatcher/read result. Unprovisioned storage refuses without File/NVS mutation or implicit format, and now preserves the entire seeded scratch sector without scratch commands. A separate explicit scratch-only entrypoint owns its own one-way episode and never initializes File/NVS. Healthy reboot/Settings readback and normal NVS GC are simulator evidence, not physical reboot qualification.

Final BIN is **80,064 bytes**, SHA-256 `85438fc2abb5e96407795dc3a0ec4dea1afb418f7d3f64e2ae30dffbc5aaf4c0`; ELF SHA-256 `7193cd2c74bec66e76c20b2c07699730039685e86a2e3bbc44534956abbf1346`. The renewed selected busy graph has 28 ITCM functions and conservative all-frame sum 484 bytes against the 1024-byte operation stack. The new bounded read-result copy is in ITCM with no callee, after known-ready return. This combines limited automated inspection with manual conditional/alias/stack/data review; it is not a general XIP proof or measured stack use.

The [manual gate](evidence/rt1060-storage-integration/verification.md), [source/artifact identities](evidence/rt1060-storage-integration/verification.json) and independent review record retain scope and reproducibility. Previous [ROM candidate evidence](evidence/rom-storage-candidate/verification.md) stays historical; its smaller image hashes are not this image.

## Remaining approval and safety gates

Physical verdict remains **NOT READY**. Fitted part/revision limits and timing margin, actual clock/timer progress/ownership, inherited Watchdog/reset state, NOR status/protection/write acceptance, cache/master/probe exclusion, MMIO stalls and the exact first-write/backup/recovery procedure remain unqualified. A proxy's device-ready state does not qualify hardware. Simulator workloads do not establish whole-media integrity, worst-case capacity or duration, or power-loss safety.

Actual command admission, controller startup, production patch/provider/layout adoption and physical operation each retain their approval gates. Maintenance-only is a limited qualification profile; M7 live controller/network/storage operation remains unqualified. No PR merge or Issue closure is included.
