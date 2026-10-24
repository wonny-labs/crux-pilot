# Scratch-separated software verification

This supplements the historical [integration gate](verification.md). Implementation `32632f2` includes scratch separation `a380b7f` and the retained semantic-startup test repair `5cb6a17`; the complete review delta begins at `d124f13`. No driver, island, boot configuration, public API, provider selector or general default changed in this follow-up.

Normal storage, including explicit fresh-media provisioning and unprovisioned failure, submits no scratch commands and preserves all 4096 seeded scratch bytes. The sample-private `crux_candidate_scratch_trial()` is selected explicitly, initializes only its scratch proxy, and performs one sector erase, one four-byte program and readback. It never provisions/mounts/saves File or Settings. Its zero result certifies only scratch readback. Both native and maintenance lifetimes are consumed on success or error; persistent APIs and either trial's rerun remain refused. The unopened Settings gate makes `maintenance_finish()` return false, which is expected for this scratch-only episode; its lifetime is still ended. Normal storage retains its existing requirement for both finish gates.

This four-byte simulator exercise is distinct from the proposed physical 16-byte first-write envelope. Actual submit/prerequisite remain `-EACCES`, elapsed remains `true`, and `main` is inert.

## Results

| Check | Result |
| --- | --- |
| Full real-engine modes, branch and disposable union | Five 1/1 modes each: normal, refused, unprovisioned, scratch, scratch-fail |
| Existing maintenance / LittleFS / readiness on union | 10 / 7 / 18 PASS, no skips |
| Focused Python suites with each exact ARM image | 12 + 33 + 7 + 26 + 3 + 15 = 96 PASS, no skips |
| Current-main Host / finite sample | 115/115 PASS / PASS |
| Current-main external application and build boundaries | 67 PASS; two opt-in skips |
| Geometry/owner fences / dependency policy / formatting | 8 PASS / 68 portable sources PASS / clang-format 18.1.8 PASS |

The two skips are `SerialCompositionTest.test_static_bindings` and `LoggingCompositionTest.test_rejected_compositions`, whose opt-in environment controls were unset. Their full unchanged rejection matrices were not expanded for this focused work. The seven active build suites include external applications, consumer/provider boundaries and native-interface checks. Existing broader fault, capacity and physical/concurrency limitations remain; no global zero-skips claim is made.

The old `test_actual_startup_verifies_before_native_post_kernel_entry` failure (`KeyError: __init___device_dts_ord_546`) is retained. The existing repair derives actual device ordinals from generated devicetree, checks device/init bytes and verifies the kernel's deferred-init skip. The complete 26-method suite passes for both current images. The manifest explicitly hashes its three test files plus the pinned device-layout and kernel-init witnesses. Earlier artifact namespaces, failures and skips remain untouched.

## Disposable integration and image scope

The exact observed main is `69e26c7b029016fe48c016953a865c9c763b5b94`. A virtual, dependency-ordered union adds PR #51, #56, #59, #62, #63 and the final #66 implementation, yielding tree `0ae599875d7f3ad49479c077b28f853f72dcf0f9`. PR #51's 58-line plan is its exact blob. Four additive conflict paths were resolved: File CMake retains main's Linux naming plus the existing lock source/linkage, module CMake/Kconfig retain both main Serial/Logging and candidate File blocks, and both README sections remain. Nine component subtrees (Time, Thread, Sync, Queue, Monotonic, Serial, Logging, Sockets, Netif) are exact main blobs. No branch/PR merge or production adoption occurred.

Both BINs are **80,120 bytes**, SHA-256 `691b043eedee25040a132cbb96f8c9f8e7ec74c8f16e0acb0e905145a0717c9c`. Branch ELF SHA-256 is `dd09ad1cc107997002eedaba46ae90e54e999a715ee21118e39fe9c089801b03`; union ELF SHA-256 is `ac9b723dc33915f19e4ca1cf26f9761bea58e656ed47936a24bf7601e8f501a2`. BIN equality does not imply ELF identity.

The fresh RAM comparison retains exact bytes, addresses, sizes and types for 44 selected code/data symbols. The selected 28-function busy graph and conservative 484/1024-byte all-frame sum retain the prior scoped manual proof. Whole relocated sections differ: 52 ITCM bytes belong to unselected native init/callbacks/veneers; two DTCM bytes are original native controller/clock pointers moving by 56 bytes to the same devices. None overlaps selected busy code/data; the busy binder constructs separate private metadata. This is scoped static inspection, not a whole-XIP proof or measured stack bound.

The full-storage fixture delegates modeled command submissions directly to Zephyr Flash simulator. Separate controlled-leaf tests exercise the actual C dispatcher/read-result implementation, and ARM inspection checks the retained image; Cortex-M instructions and physical Flash were not executed.

## Reproduce and retain

Use the pinned workspace/SDK and build commands in [verification.md](verification.md), selecting a fresh writable output namespace. Run every mode in a separate native process to preserve one-way lifetime isolation:

```sh
./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram
./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram -testargs refused
./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram -testargs unprovisioned
./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram -testargs scratch
./zephyr/zephyr.exe -flash_in_ram -flash@0_in_ram -testargs scratch-fail
```

Local project namespace `outputs/m4-conditional66` retains the exact union preparation, source/critical pinned/config/staged hashes, both ELF/BINs, compile records, disassemblies, selected-delta audit, raw RED/GREEN/failure/skip logs, scripts and compact archive. [The index](conditional-acceptance.json) records source refs, counts, archive and image hashes. The archive has 184 files; all 183 manifest artifact entries match both archived and live bytes. Its SHA-256 is `ad2b2e5e7e8e9826cc23770ef3b8eb37c24ae64502a72f61e2c150dcd9d1d0ad`. Curated critical-input hashes do not claim a complete transitive header/toolchain inventory.

Local initial failures were preserved and corrected: missing disposable Git metadata for repository scripts, unavailable formatter command alias, and fixture-only formatting violations. Expected TDD failures showed implicit scratch work and the absent scratch-only implementation. Final engineering review and hosted CI are separate gates. Physical verdict remains **NOT READY**; no target admission, programming/boot/reset, controller startup, production adoption, M4 completion, M5 physical/concurrency qualification, merge or issue closure is authorized or demonstrated.
