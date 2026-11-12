# TCP initial sequence number that fails closed (row C11, decision D-c)

Row C11 of stage 2 of the [RT1060-EVKB bench image](rt1060-management-image-feasibility.md). It closes the follow-up of [row C5](rt1060-wolf-entropy.md#follow-up): Zephyr's `tcp_init_isn` ignores a failed `sys_csrand_get`.
Refs #183.

**Everything here ran on the Host, on `native_sim`, or as a compile and link for `mimxrt1060_evk@B//qspi`. Nothing ran on a board.**

**Update (T11 of [the freeze design](rt1060-bench-freeze-and-capture-design.md)).** This row first shipped with a sibling declaration block `patched_zephyr.isn_fail_closed` and a second marker `.crux-isn-fail-closed` (decisions 8 and 10 below). T11 moved the patch into the design's ordered list and replaced the two markers by the **one** marker `.crux-wolf-tls` with `<pin> <tls sha256> <isn sha256>`, which `scripts/prepare-isn-fail-closed.py` now writes by extending the TLS step's marker, with the owner's approval of that one edit to a protected script (see [how this relates to the freeze design](#how-this-relates-to-the-freeze-design)). The sections *How the copy is made* and *Decisions for review* are current, and so is the table of *The guards*; its mutant count is the C11 figure at merge, and the T11 figure is stated beside it. The *Proof* section and the evidence folder record what ran when the row merged, with two markers, and stay as they were.

## The owner's decision

> Owner, 2026-11-09, in the session that manages stage 2 of the bench image: "1-8까지 너의 추천대로 진행해줘". The recommendation for D-c was: **patch** the bench image's build-local Zephyr copy, not a check in the composition and not acceptance.

Decision D-c was open in [the image plan](rt1060-bench-image-plan.md#what-blocks-the-plan-today) and in [`board-image-profiles.md`](board-image-profiles.md). This record is its implementation.

## Result

| Question | Answer | Evidence |
| --- | --- | --- |
| Does the pinned Zephyr keep a zero key after a failed read? | **Yes, but that is not the whole finding.** The read is ignored and the key stays zero, as C5 said. In addition the pinned code **never uses the key**: it asks PSA for a SHA-256 digest (32 bytes) into a 16-byte buffer, PSA answers `PSA_ERROR_BUFFER_TOO_SMALL` for both providers (Mbed TLS and wolfPSA), and that answer is ignored too. The initial sequence number is then the uptime plus a buffer that holds no hash: with **wolfPSA** (the bench image) uninitialised stack, with **Mbed TLS** (the native_sim test) `0x21212121`, because it fills the buffer with `'!'` before it refuses. The zero key is **latent**: it is what a fix of the buffer alone would expose. | [The two defects](#the-two-defects); the red run's `isn=unkeyed` even after a read that works |
| What does the patch do? | A missing key or a failed hash makes `tcp_init_isn` return an error. A SYN to a listening port gets a **RST**; `net_context_connect()` returns **`-EIO`** and sends no SYN. The next attempt reads the key again. When the read works, the number is the hash of the key and the tuple plus the uptime, as the code means. | [Behaviour](#what-the-stack-does-now) |
| Is it unchanged when the read succeeds? | **No, on purpose, in two ways.** The formula and the call order are as upstream intends, but (a) the numbers change: they are now made from the key, where they were a constant or stack plus the uptime; and (b) a failed `psa_hash_compute` now refuses the connection, which upstream ignored. **The main availability risk of the patch is wolfPSA's hash at run time on the board**: it was never run, and if `psa_hash_compute` fails there (for example `PSA_ERROR_BAD_STATE` before PSA is initialised), every new HTTPS and SSH connection is refused. This departs from "Zephyr behaviour unchanged when the read succeeds" and needs the owner's explicit acceptance ([decision 2](#decisions-for-review)). | [Patch](#the-patch), [Not covered](#not-covered) |
| Was it proved? | Three ways, below. | [Proof](#proof) |
| Does it change the web, SSH or serial behaviour? | New TCP connections to the board are refused when the key read or the hash fails (HTTPS and SSH clients see a reset). With a working read and hash nothing is refused, and the visible change is that the initial sequence numbers are made from the key. Established connections are untouched, and the serial line and UDP do not use TCP. | [Behaviour](#what-the-stack-does-now) |

## The two defects

Both are in `subsys/net/ip/tcp.c` of Zephyr v4.4.2 (`dccb0959`), SHA-256 of the file `393b940e0d96adba9a0f7667c061b9e0e5d8d0ea2f49642e60f29ce1efdb7ad6`.

| # | At the pin | Consequence | Found by |
| --- | --- | --- | --- |
| 1 | `tcpv4_init_isn` and `tcpv6_init_isn` each call `sys_csrand_get(unique_key, sizeof(unique_key))` once, ignore its result and set `once = true` | After a failed read the 16-byte key stays all zero for the rest of the boot | C5's call-site scan; [C5 follow-up](rt1060-wolf-entropy.md#follow-up) |
| 2 | `uint8_t hash[16]; psa_hash_compute(PSA_ALG_SHA_256, …, hash, sizeof(hash), &hash_len);` and the result is ignored | A SHA-256 digest is 32 bytes. Mbed TLS PSA and wolfPSA (`psa_hash_finish`, `hash_size < expected_hash_size`) both return `PSA_ERROR_BUFFER_TOO_SMALL`. wolfPSA writes nothing, so `hash` is uninitialised stack; Mbed TLS first fills the buffer with `'!'` (`psa_crypto_hash.c`, `mbedtls_psa_hash_finish`), so `hash` is a constant. **Either way the sequence number is made from no key** | C11's native_sim red run, then read in the Mbed TLS and wolfPSA sources |

Defect 2 changes what the patch has to do. A check of the key alone, with the 16-byte buffer left, would have refused **every** connection
the moment it also checked the hash result, and without that check the key would still be unused. So the patch fixes both together.
The upstream status of either defect was not checked.

```mermaid
flowchart TD
    subgraph before["Pinned Zephyr"]
        b1["tcp_init_isn: first call"] --> b2["sys_csrand_get(key): result ignored, once = true"]
        b2 --> b3["psa_hash_compute(SHA-256, hash[16]): refused, result ignored"]
        b3 --> b4["ISN = uptime + a buffer that holds no hash<br/>(wolfPSA: stack, Mbed TLS: constant)"]
        b4 --> b5["SYN-ACK or SYN is sent, whatever the read did"]
    end
    subgraph after["With the patch"]
        a1["tcp_init_isn"] --> a2{"key read before?"}
        a2 -- no --> a3["sys_csrand_get(local buffer)"]
        a3 -- failed --> a4["return the error, once stays false"]
        a3 -- ok --> a5["copy to unique_key, once = true"]
        a2 -- yes --> a6["psa_hash_compute(SHA-256, hash[32])"]
        a5 --> a6
        a6 -- failed --> a4
        a6 -- ok --> a7["ISN = hash of key and tuple + uptime"]
        a4 --> a8["passive: RST, no connection<br/>active: connect returns the error, no SYN"]
    end
```

## What the stack does now

| Path | Caller | Before | With the patch | Why it is safe |
| --- | --- | --- | --- | --- |
| Passive open (a SYN to a listening port) | `tcp_conn_new`, which `tcp_recv` calls | A SYN-ACK with a number from no key | The new context is released with `net_context_put`, `conn = NULL`, `goto err`. `tcp_recv` then sends a **RST** (`net_tcp_reply_rst`), and the drop is counted (`conndrop`) | The same three lines as a failed `net_context_bind` just above and a failed `net_conn_register` just below use at this point |
| Active open (`net_context_connect`) | `net_tcp_connect` | A SYN with a number from no key | `goto out` with the error, **before** `net_context_set_state(CONNECTING)`, the connection registration and the handshake. Nothing is sent, and the caller sees the negative error (`-EIO` from `sys_csrand_get`; `-EIO` for a failed hash) | The same exit as the earlier `-EINVAL` returns of the function, before any state is set |
| Another address family, `CONFIG_NET_TCP_ISN_RFC6528=n` | `tcp_init_isn` | `sys_rand32_get()` | Unchanged (`sys_rand32_get()`). The bench guards require the setting to be on, so the patch protects something | [Guards](#the-guards) |

| Effect on the bench image | With a failed key read |
| --- | --- |
| HTTPS (443) and SSH (22) clients | A new connection is reset. The listener stays up. The next SYN reads the key again, so a read that works later makes the next connection succeed |
| Connections that already exist | Untouched: they have their number |
| The serial line (the CRUX-ENTROPY and bench report lines) | Not TCP: unaffected. The entropy line of C5 already reports a failed first read at boot |
| UDP, ICMP | Not affected |
| The log | One `NET_ERR` per refusal ("Cannot read the key of the initial sequence number" and "No initial sequence number … refused") |

A refusal per SYN costs one read of 16 bytes from the TRNG, so a flood of SYNs against a failing TRNG repeats the failing read. That is the safe direction for the bench, and a board run will show whether the TRNG fails after a start at all.

## The patch

`patches/zephyr/zephyr-isn-fail-closed.patch`, SHA-256 `831fecdbc6cfb23f47ace4dda662a95222b977977a167ef786ee02f33712de87`, one file, 86 lines added and 34 removed (most of it the two caller blocks, the key function and the changed signatures).

| Hunk | Change |
| --- | --- |
| New `tcp_isn_key_get()` | Reads the key into a **local** buffer, copies it into `unique_key` and sets `once` only after a successful read. A failed read never leaves a partly written key in use |
| `tcpv6_init_isn`, `tcpv4_init_isn` | Return `int` and the number through a pointer. The `once` blocks go (the key is read by `tcp_isn_key_get`). The digest buffer is `PSA_HASH_LENGTH(PSA_ALG_SHA_256)`, and a failed `psa_hash_compute` returns `-EIO` |
| `tcp_init_isn` | Returns `int`, calls `tcp_isn_key_get()` first. `sys_rand32_get()` for the no-RFC 6528 case, as before |
| The `#else` stubs | `tcp_isn_key_get()`, `tcpv4_init_isn(...)`, `tcpv6_init_isn(...)` expand to `(0)` |
| The two callers | The refusals of [the table above](#what-the-stack-does-now) |

Limits of the patch: `once` is a plain `static bool`, as it was (no memory barrier: single-core, as the bench image is). `psa_hash_compute` returns `PSA_ERROR_BAD_STATE` while PSA is not initialised, so a SYN before PSA's initialisation is refused too, which is the safe direction. The patch is not an upstream fix.

## How the copy is made

The copy is a build-local tree outside the repository. A copy that other sessions or an earlier run already use as a read-only input is **never changed in place**: make a new one.

```mermaid
flowchart LR
    pin["Zephyr checkout at the pin<br/>(west workspace, read-only)"] -->|git archive, no doc, samples, tests| copy["COPY<br/>(no .git)"]
    copy -->|"scripts/prepare-wolf-tls-zephyr.sh COPY<br/>wolfSSL TLS socket patch, D8"| tls["COPY + .crux-wolf-tls<br/>sockets_tls.c 106acdbb…"]
    tls -->|"scripts/prepare-isn-fail-closed.py COPY<br/>this patch, D-c"| both["COPY + .crux-wolf-tls extended<br/>pin, tls sha, isn sha<br/>tcp.c 94642fe2…"]
    both -->|ZEPHYR_BASE| build["bench build: the guards check the one marker and every patched file's hash at configuration"]
```

| Step | Command | Checks |
| --- | --- | --- |
| 1 | `scripts/prepare-wolf-tls-zephyr.sh COPY` | The checkout is at the `west.yml` pin; the osp patch's SHA-256 `3d1cc030…`; `sockets_tls.c` SHA-256 `106acdbb…` |
| 2 | `scripts/prepare-isn-fail-closed.py COPY` | `COPY` is not a Git checkout; the patch file's SHA-256 `831fecdb…`; `.crux-wolf-tls` is exactly `<pin> <tls sha256>` and a copy with the earlier `.crux-isn-fail-closed` is refused, each with nothing written; `tcp.c` is the pinned file (`393b940e…`) before and `94642fe2…` after; the marker becomes `<pin> <tls sha256> <isn sha256>`; a patched `tcp.c` without that marker is refused; a second run changes nothing |
| 3 | Build with `ZEPHYR_BASE=COPY` | `bench_guard.cmake` builds the expected marker from the declared ordered list (`<pin>` and the patches' hashes), compares the one marker, refuses any other `.crux-*` marker, and checks every patched file's final hash from `bench_declaration.json` |

The marker is the order of the patches: step 2 starts only from the marker that step 1 wrote, so a copy made in another order, with a step missing or with an extra one has another text, and the guard and the freeze refuse it.

**Standalone mode.** `scripts/test-isn-fail-closed.sh` patches a bare copy of the pinned tree for `native_sim`, with no TLS marker. The script then applies the patch and writes **no marker at all** (no marker format is invented for it). The bench guard refuses such a copy because the marker is missing, and a second run of the script on it is refused, because a patched `tcp.c` with no marker cannot be told from a copy whose marker was removed.

**Re-running step 1 on a finished copy** finds a marker that is not `<pin> <tls sha256>`, so it makes the copy again from the pinned tree, and step 2 must run again.

The order is the TLS patch first. The two patches touch different files; step 2 would refuse a TLS patch that had touched `tcp.c`, because it checks the original's hash. The manifest of the build records the block of the design (`patched_zephyr`, schema 2).

**What whoever builds the bench image next must do.** The board image is built later from a pinned commit by the main session. Make a **new** copy with the two commands above (do not use a copy that only has the TLS patch: configuration refuses it), build from the pinned commit, and keep the manifest. A freeze that pins stock Zephyr records a tree that was not compiled ([blocker B2](rt1060-bench-image-plan.md)): it now has two patches to account for, and the plan's options for the copy apply to both.

### How this relates to the freeze design

[`rt1060-bench-freeze-and-capture-design.md`](rt1060-bench-freeze-and-capture-design.md) (merged as #373) specifies, for the freeze of the copy, a declaration with an **ordered list** of patches, a list of every patched file with its final hash, a declared tree digest and one marker that carries every patch's hash. The patch has one file (`subsys/net/ip/tcp.c`), changed by this patch only and not by the TLS patch (the copy's `tcp.c` is the pinned file after the first patch, [evidence](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/copy/tls-copy-hashes.txt)), applied second.
T11 migrated this row to that shape: `bench_declaration.json` holds `patched_zephyr` with `"schema": 2`, the two patches in order, both patched files, the one marker file, and `requires_settings` (the RFC 6528 setting, decision D-c, row C11) and `stock_sha256` (`tcp.c` before the patch), which only the guard and the tests read. The block that `bench_guard.cmake` writes into the manifest has the design's keys. Two things are **not** done there, on purpose: the tree digest and its file count are `null` until two independent derivations on a real build copy agree, and the TLS patch (which is not in this repository) is declared only with the one file that the repository records for it, so the freeze and the image check refuse the declaration as it stands (`open_items` in the declaration says so).

Two things about the new marker flow are **unverified**:

- **`patch --strip=1` against `git apply --no-index`.** The freeze re-derives the tree by applying every patch with the `patch --strip=1 --no-backup-if-mismatch --quiet` command line of the TLS script (design step 4(e)), while `scripts/prepare-isn-fail-closed.py` applies this patch with `git apply --no-index` (and checks the result's hash). Both are assumed to give the same `tcp.c`; that has not been shown for this patch.
- **No real run.** The flow `prepare-wolf-tls-zephyr.sh`, then `prepare-isn-fail-closed.py`, with the guard and the freeze after it, has never run on a real copy with the real TLS script (which downloads its patch). The host tests use a synthetic TLS marker.

**Crash and leftovers.** The script replaces `tcp.c` first and the marker second. A crash between the two writes leaves a patched `tcp.c` under the TLS-only marker: the script refuses that copy and it cannot be finished, so make a new copy. A stale `.crux-wolf-tls.new` that a crash leaves behind is a `.crux-*` file that is not the marker, so the bench guard refuses the copy for that reason too.

## The guards

The existing guards keep every check. New checks, each with a case that fails without it and a mutant that is killed:

| Level | New check | Case that must be refused |
| --- | --- | --- |
| CMake (`bench_guard.cmake`, step 9) | The one marker `.crux-wolf-tls` is `<pin> <hash 1> <hash 2>` in the declared order; no other `.crux-*` marker is there; every patched file has its declared final hash (a deleted one is absent) | A copy without the marker, on another pin, with a missing, an extra, a changed or a reordered patch hash, with another marker beside it (the earlier `.crux-isn-fail-closed` among them), with a patched file missing or different, an unpatched tree, a deleted file that is still there |
| CMake (`bench_guard.cmake`, step 9b) | `CONFIG_NET_TCP_ISN_RFC6528` is set (`requires_settings`); `CONFIG_NET_TEST` and `CONFIG_NET_TEST_PROTOCOL` are not (they make the stack skip `tcp_init_isn`) | No RFC 6528 setting, either test setting |
| C (`src/bench_guard.h`) | `#error` without `CONFIG_NET_TCP_ISN_RFC6528`, and with either test setting | The same three |
| Kconfig (`Kconfig.bench`) | `CRUX_MANAGEMENT_BENCH_IMAGE depends on NET_TCP_ISN_RFC6528` | The setting off: the declaration and every test form drop to `n` |
| Declaration | `patched_zephyr` schema 2, with this patch as the second entry of `patches`, its file in `patched_files`, and `requires_settings` | `tests/build/test_management_bench_declaration.py` keeps it equal to the scripts, the patch file and `target.conf`, and the freeze's own `validate_block` reads the block it becomes |
| Manifest | The block `patched_zephyr` (schema 2) and `CONFIG_NET_TCP_ISN_RFC6528` in `settings` | `check_manifest` in `check_bench_guard.py`, also for a third patch and a deleted file |

**T11 figure (local, Host):** `check_bench_guard.py --skip-kconfig` kills **128 of 138 mutants (0 survived)**, and the 10 Kconfig mutants were skipped because Kconfiglib is not installed locally; the Kconfig level and CI have not been run for T11 yet.

**C11 figure at merge (historical):** `check_bench_guard.py` mutants: **127 of 127 killed**: the 104 of the earlier rows stay killed, and 23 are new (the new `message(FATAL_ERROR)` lines, and one weakening of each new comparison, condition and manifest line, in `bench_guard.cmake` and in `bench_guard.h`). The new cases were run first against the original guard files and all failed there ([red run](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/host/check-bench-guard-red-original-guards.txt))..

## Proof

### 1. The patch applies cleanly to the pinned tree, and every hash matches (Host, red first)

| Check | Result | Evidence |
| --- | --- | --- |
| `test_prepare.py`, before the script existed | Red: 10 of 10 errors | [`host/test-prepare-red-no-script.txt`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/host/test-prepare-red-no-script.txt) |
| `test_prepare.py` with the patch and the script | Green: 10 of 10. The pinned `tcp.c` has the hash the patch is for; `git apply --check` and the apply give the declared result hash; the declaration, the script and the patch file agree; one changed byte in the source, a drifted patch, a Git checkout, and a patched file without its marker are all refused and leave the tree alone; a second run changes nothing; the result has the key check and the 32-byte digest and no call that ignores the read | [`host/test-prepare-green.txt`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/host/test-prepare-green.txt) |
| The scripts on a real copy (Dev Container) | `prepare-wolf-tls-zephyr.sh` leaves `tcp.c` at `393b940e…`; `prepare-isn-fail-closed.py` makes it `94642fe2…` and writes the marker; a second run is `already prepared`; the workspace's Zephyr checkout is refused | [`copy/`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/copy/prepare-isn.txt) |

### 2. A TCP connection attempt is refused when `sys_csrand_get` fails after the start (native_sim, red then green)

`tests/native/isn_fail_closed` builds Zephyr's real network stack on `native_sim` with a dummy interface, the real `CONFIG_HARDWARE_DEVICE_CS_GENERATOR` `sys_csrand_get()` over a test entropy device whose reads fail on request, and Mbed TLS PSA for the hash. The test starts with the device working (so PSA initialises, as after a good boot check), then (A) makes every read fail, injects a SYN from a peer and calls `net_context_connect`, then (B) makes the read work again and does both once more. It reads what the stack sends in the device's send function, and it classifies each sequence number as made from the key the device gave (**keyed**), from the all-zero key (**zero-key**), or from neither (**unkeyed**).
`scripts/test-isn-fail-closed.sh` runs it on the pinned tree and on a copy with the patch, and requires the first to fail and the second to pass.

| Phase | Path | Unpatched pinned tree (red) | Patched copy (green) |
| --- | --- | --- | --- |
| A, the read fails | SYN to a listening port | `SYN|ACK`, number **unkeyed** | **`RST|ACK`**, no connection accepted |
| A, the read fails | `net_context_connect` | returns `-EINPROGRESS` (-115), a **SYN is sent**, number **unkeyed** | returns **`-EIO` (-5)**, **nothing is sent** |
| B, the read works again | SYN to a listening port | `SYN|ACK`, number **unkeyed** | `SYN|ACK`, number **keyed** |
| B, the read works again | `net_context_connect` | SYN sent, number **unkeyed** | SYN sent, number **keyed** |

The red run is the finding of [the two defects](#the-two-defects): the pinned stack answers and connects with a failed read, and even with a working read it never makes the number from the key, because the hash is refused. **The red run does not show the zero key itself**: with defect 2 present the zero key is never reached (`zero-key` is a classification of the test that no run produced). The zero key is latent, and what the red run proves is that nothing refuses.
The green run shows the key check and the hash fix together: refusal when the read fails, recovery when it works, and a keyed number that equals the hash of the key and the tuple plus the uptime.

| Run | Exit | Evidence |
| --- | --- | --- |
| Unpatched | 1 (`PROJECT EXECUTION FAILED`; the script requires it) | [`native-sim/unpatched-run.txt`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/native-sim/unpatched-run.txt) |
| Patched | 0 (`PROJECT EXECUTION SUCCESSFUL`) | [`native-sim/patched-run.txt`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/native-sim/patched-run.txt) |

### 3. The target cross-build, the guards and the call sites in the ELF (Dev Container, compile and link only)

Two builds of the bench image for `mimxrt1060_evk@B//qspi` from clean detached checkouts, with a throwaway first-account password file made by C4's generator outside every repository (deleted afterwards), the C1 recipe (`-j2`, the Dev Container under emulation) and the same module pins.
**Before** is `origin/main` `52e894e8` on a copy with the TLS patch only. **After** is this branch at `8314cc75` (the code of the head; only documents and the evidence were added since) on a copy with both patches.

| | Before | After |
| --- | --- | --- |
| The guards in the configure | passed | passed, including the new ones; the manifest records `patched_zephyr.isn_fail_closed` and `CONFIG_NET_TCP_ISN_RFC6528=y` |
| Flash (`text` + `data`) | 562,212 B (557,864 + 4,348) | **562,296 B** (557,948 + 4,348): **+84 B** |
| RAM (`data` + `bss`, all SDRAM) | 2,171,998 B (4,348 + 2,167,650) | 2,171,998 B: **unchanged** |
| `zephyr.bin` SHA-256 | `e9af5b81…` | `5aa5fa23…` (one run of each; both builds embed the same throwaway password, manifest `first_account.file_sha256` `aa3a5d96…` in both, so these hashes are not comparable with a build that uses another password) |
| Warnings | 4 (wolfSSH `wolfsftp.c`, wolfPSA `psa_key_storage.c`) | the same 4, none in `tcp.c` |
| C4's scan for the throwaway password | clean | clean |
| Negative control: the real configure of the after tree on the copy with the TLS patch only | | **refused**: `.crux-isn-fail-closed is missing` ([log](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/copy/control-tls-only-configure.log)) |

The call-site scan ([`call-sites.txt`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/build/after/call-sites.txt)) lists the same functions before and after (`tcp_init_isn` is the one caller of `sys_csrand_get`). The disassembly of `tcp_init_isn` shows the difference:

| | Before ([`disasm`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/build/before/disasm-tcp_init_isn.txt)) | After ([`disasm`](evidence/rt1060-management-image-feasibility/isn-fail-closed-c11/build/after/disasm-tcp_init_isn.txt)) |
| --- | --- | --- |
| After the call to `z_impl_sys_csrand_get` | `strb r5, [r4]` stores `once = 1` at once; `r0` is never read | `subs r4, r0, #0` and `blt` to the exit: the result is tested **before** the key is copied and `once` is set |
| Where the read goes | straight into `unique_key` | into a stack buffer (`add r0, sp, #40`), copied after a good result |
| Digest buffer for `psa_hash_compute` | `movs r3, #16` | `movs r3, #32` |
| After the call to `psa_hash_compute` | the result is dropped; the number is read from the buffer (`ldr r4, [sp, #12]`) | `mov r4, r0` and `cbnz r0`: a failed hash returns `-5` (`mvn.w r4, #4`, `-EIO`) and the number is written through the pointer only on success |


## Not covered

| Not covered | Why, and what covers it |
| --- | --- |
| A board | No board operation is allowed here. Whether a TRNG read fails **after** a successful start on the RT1060-EVKB is seen only on a board; this row only makes the stack safe if it does |
| wolfPSA at run time | `native_sim` uses Mbed TLS PSA (the optional Mbed group) for the hash. wolfPSA's `psa_hash_finish` returns `PSA_ERROR_BUFFER_TOO_SMALL` for a 16-byte buffer by its source (read, not run). The bench image **links** with it, and the disassembly shows the 32-byte buffer and the tested result |
| The board's CSPRNG path | The board's `sys_csrand_get` is `CONFIG_PSA_CSPRNG_GENERATOR` over wolfPSA; the native test uses `CONFIG_HARDWARE_DEVICE_CS_GENERATOR` over a test device. Both return a negative value on failure, and `tcp.c` only tests that. C5 shows the board's path fails with `-EIO` |
| IPv6 | The bench image has IPv6 off (a guard), and the native test too. The IPv6 functions are compiled (they sit in the same block) and have the same change; they were not run |
| Concurrency | `once` is a plain `static bool`, as upstream, with no memory barrier. Two first SYNs racing may both read the key, and the later copy replaces the earlier one, even after numbers were made with it, and a copy that is preempted may be read half old and half new. Each number is still made from a good random key, so this is not a prediction risk, but the key is not constant. Upstream's race is worse (a read in progress writes the key in use) |
| A slow or failing TRNG read | A refused SYN costs one failing read in the receive path, once per SYN. If a failing MCUX TRNG read is slow, a flood of SYNs could stall reception. Not measured; a board run reads it |
| A hash failure that is not a size | `PSA_ERROR_BAD_STATE` before PSA's initialisation also refuses (the safe direction) and is not exercised |
| The other random users | `sys_rand_get()` in `tcp_derive_rto`, `find_available_port` and `eth_nxp_enet_init` still falls back to the cycle counter when the TRNG fails ([C5](rt1060-wolf-entropy.md#every-place-that-could-fall-back)): not keys, and not changed here |
| The CI workflows | The new native test needs the optional west group (Mbed TLS) and a Linux container, and `.github` is protected: no workflow runs it. The guard script and the declaration test run where they ran before; `test_prepare.py` needs `ZEPHYR_BASE` |
| Upstream | Whether Zephyr has fixed the two defects after v4.4.2 was not checked. The patch is a build-local copy until it is |
| A freeze of the image | The plan's blocker B2 (a freeze that sees the copy) is not solved here: the copy has two patches now |

## Decisions for review

| # | Decision taken for lack of an answer | Why, and the other way |
| --- | --- | --- |
| 1 | **The patch file lives in `patches/zephyr/`**, which is not a protected path. The TLS patch is not in the repository at all (the scripts fetch it from its upstream by revision), so there was no existing in-repo place for "the patch beside it". `patches/zephyr/` already holds Crux's own patches of Zephyr files (the Mbed TLS entropy, the MDIO and the MMC corrections) with a README | A different place needs only a move and the three hashes' file name. Reported to the main session in the hand-over |
| 2 | **The scope grew to defect 2, the 16-byte digest buffer, and the patch now also refuses on a failed hash. This needs the owner's explicit acceptance.** A check of the key alone would leave the key unused, and a check of the hash result with the old buffer would refuse every connection | Without it the task's goal (a failed read does not lead to a predictable number) is not met in a way the red run can show. It is a security-relevant scope expansion (`AGENTS.md`: escalate before adopting), and it adds an availability dependency on wolfPSA's hash at run time. The other way: fix only the key check and accept that the number stays keyless, which makes the patch cosmetic |
| 3 | A refused passive open is a **RST** and a refused connect returns the error of the read (`-EIO`), sending nothing | These are the stack's own exits for a failed bind and for the early errors of `net_tcp_connect`. Dropping the SYN silently is the other choice: it needs a different exit and leaves the peer waiting |
| 4 | The key is read again at **every** attempt until a read works, rather than latching the failure | A TRNG that works again is then used. The cost is one failing read per SYN |
| 5 | The guards also refuse `CONFIG_NET_TEST` and `CONFIG_NET_TEST_PROTOCOL` | Both make the stack skip `tcp_init_isn()`, so the patch would protect nothing. Neither is in the bench |
| 6 | The new script is `scripts/prepare-isn-fail-closed.py`, a new file, and it does **not** edit `prepare-wolf-tls-zephyr.sh`, which is on a protected path (T11 edited the new script, and still not the TLS one) | The bench copy is made with two commands in a fixed order. A single script for both needs an approved edit of the protected one |
| 7 | The script refuses a Git checkout and changes the tree **in place** | A copy that other sessions use as a read-only input is never changed: the next copy is made new |
| 8 | The decision keeps the plan's label **D-c** and the declaration says `"row": "C11"`; the marker was `.crux-isn-fail-closed` (**superseded by T11**: the one marker `.crux-wolf-tls`, see decision 10) | Names follow `.crux-wolf-tls` and the existing `D8` block |
| 9 | The native test classifies numbers by recomputing the hash. The passive path hashes with the port that `net_context_get` chose at random for the new context and `net_context_bind` writes back into `tcp_conn_new`'s address when its port is 0 (`net_context.c`, the `addr4->sin_port = net_sin_ptr(&context->local)->sin_port` line), so the test tries all 65,536 ports (a first version that assumed port 0 did not match the keyed number) | The port is not observable from outside; the cost is a fraction of a second |
| 10 | **Two markers when the row merged, not the single extended marker of the freeze design (#373). Superseded by T11: one marker.** | The existing guard compared `.crux-wolf-tls` to one exact text and the row kept the D8 entries and checks as they were. T11 (owner's approval of one edit of `scripts/prepare-isn-fail-closed.py`, 2026-11-12 21:43:57 UTC, "Approve option B") made this script extend the TLS step's marker, and folded the two blocks into the ordered list. The TLS script is unchanged: its own "already prepared" test no longer matches a finished copy, so it makes the copy again (step 1 above) |

Approvals needed: the owner's review of the PR and the independent review. A merge needs the owner's explicit authorisation (`AGENTS.md`).
