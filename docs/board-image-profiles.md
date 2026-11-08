# Image profiles for the RT1060-EVKB program and restore path

Status: **proposed**, software only. M5 and S2 (see below) are the registered profiles. Since the capture change a profile also says whether and for how long the `program` mode records the UART of the image it starts ([UART capture in `program` mode](#uart-capture-in-program-mode)). This document and its code change nothing about what may be done on the board. No programming, reset, cable operation or merge is authorised by it. Every physical use of a new image still needs its own approval, reviewed plan and the root mount guard described in [the physical preparation](networking-physical-preparation.md).

## Why

The reviewed freeze (`tests/target/prepare_m5_network.py`) and driver (`tests/target/program_m5_image.py`) were written for one image, the M5 network test image. Other images, such as a product image built from a separate repository, need the same safe path. Copying the tools per image would copy the safety gates too and let them drift. A **profile** separates what differs between images from the gates that must not.

## What a profile holds, and what it cannot hold

| Belongs to the profile (image facts) | Stays common and unchanged (safety gates) |
|---|---|
| `name`, recorded in the frozen manifest | Guard check before every probe command |
| `manifest_path`, the west path of the manifest repository | UART and probe ownership inventory, exclusive board lock |
| `pinned` and `pinned_paths`, the exact dependency set | Target and probe identity, target voltage |
| `check_image`, the image checks (Kconfig, stacks, geometry) | Bounded commands, series budget, no retry |
| `commit_tag`, whether the source commit is embedded in the binary | Identity-only probe first, entry-image verify before a change |
| `errno_suffix`, the source file of the errno evidence, or none | Pinned image hashes, manifest re-verification before a load |
| `tool_scripts`, the extra frozen scripts of the tool repository | One use per mode and evidence directory, restore of the accepted normal image |
| `modules` and `module_paths`, the build modules of an image built by west module discovery (none for M5) | The rule that no other project is accepted under module discovery lives in `frozen_inputs`, not in the profile |
| `capture` (`none` or `raw`) and `capture_seconds`, the bounded window of the passive UART capture of `program` (`none` and 0 for M5) | The port, the board lock and the ownership inventory (`acquire`, `preconditions`); the capture only reads what the driver already owns |

A profile is a frozen dataclass of data and one checking function. `Driver`, `check_identity`, `consume` and the precondition functions do not take a profile, so a profile has no way to relax a gate. The one profile setting that reaches the driver is the capture, and `Driver` receives it as a bounded `CaptureSpec`, not as the profile; a test pins the exact field list of `ImageProfile` and the exact parameter list of `Driver`, so a new way to influence a gate shows up as a failing test in the same reviewed change. A profile does control how strictly an image is checked (its checks, its pinned set, whether a commit tag or errno evidence is required), so a weak profile is possible. The control is that a profile only exists as a registered entry in `PROFILES`, which is a reviewed code change with its own plan review, and that the driver accepts registered names only.

```mermaid
flowchart LR
    subgraph profile[Profile: image facts]
        N[name] --- M[manifest_path] --- P[pinned set] --- C[check_image] --- T[commit tag, errno, tools]
    end
    profile --> F[prepare / verify<br/>freeze of the image]
    F --> MAN[(Frozen manifest<br/>records profile name)]
    MAN --> D[Driver: identify, program, restore]
    G[Safety gates<br/>common, unchanged] --> D
    D --> B[(RT1060-EVKB)]
```

## What is built

| Change | Where |
|---|---|
| `ImageProfile` dataclass; since the S2 change it also holds `modules` and `module_paths` and refuses an inconsistent profile (a project both pinned and a module, a module revision that is not a full commit, two projects or the manifest checkout that share or contain a path) | `tests/target/image_profile.py` |
| `frozen_inputs`, `prepare`, `verify` and `default_tools` take a `profile`; without one they use the M5 profile, built from the unchanged M5 constants | `tests/target/prepare_m5_network.py` |
| `PROFILES` registry and `profile_named`; unknown names are refused | `tests/target/prepare_m5_network.py` |
| `--profile` and `--tool-repo` options of the freeze command (`prepare`, `verify`), restricted to registered names, default M5. Before the S2 change the command could only freeze the M5 image | `tests/target/prepare_m5_network.py` |
| `--profile` option of the driver, restricted to registered names, default `m5-network-physical`; `bind_profile` gives the selected profile and the one manifest verification that `main` uses for both the first load and the re-verification before each load | `tests/target/program_m5_image.py` |
| `profile_named` refuses a registered factory whose profile name differs from its key | `tests/target/prepare_m5_network.py` |
| `prepare(..., tool_repo=...)`: the image source and the frozen tooling may live in different repositories | `tests/target/prepare_m5_network.py` |
| Tests of the mechanism | `tests/build/test_m5_image_profile.py` (13 cases) |
| The S2 profile `s2-controller-physical`: factory `s2_profile` in `PROFILES`, `S2_*` constants (manifest path, pins, modules), `s2_check_image` (the S2 checks plus the firmware identity) | `tests/target/prepare_m5_network.py` |
| The S2 image checks (Kconfig, stack objects, geometry through the shared M3 check) | `tests/target/s2_image.py` (new) |
| The module-discovery rule in `frozen_inputs` (see below) | `tests/target/prepare_m5_network.py` |
| S2 tests: 36 cases | `tests/build/test_m5_s2_image.py`, `tests/build/test_m5_s2_profile.py` |
| S2 fixtures: the real resolved configuration (normalised) and the measured facts with hashes | `tests/build/fixtures/s2-controller/` |

The M5 behaviour is unchanged: the existing M5 and M3 tests pass without modification, and an M5 manifest, plan and approval are used exactly as before.

| Change of the capture work | Where |
|---|---|
| Profile fields `capture`, `capture_seconds`, validated (`none` has no window, `raw` has a whole number of seconds from 1 to 300) | `tests/target/image_profile.py` |
| S2 registers `capture='raw'` with a 180 s window (`S2_CAPTURE_SECONDS`); M5 keeps the default `none` | `tests/target/prepare_m5_network.py` |
| `RawCapture`, `CaptureSpec`, the pure `line_view` and `analyse`: exact raw bytes, a timestamped line view, classification | `tests/target/uart_raw_capture.py` (new) |
| `Driver(capture=...)`, the `before` hook of `Driver.run`, `Driver.program_captured`, `capture_spec(profile, mode)`; `main` passes the profile's spec to the driver | `tests/target/program_m5_image.py` |
| Capture tests (25 cases) and driver and profile tests (29 cases) | `tests/build/test_m5_raw_capture.py`, `tests/build/test_m5_program_capture.py` |
| A real capture as fixture, copied unchanged from `docs/evidence/networking-physical-stage1-run3/.../restore/normal-capture` with hashes | `tests/build/fixtures/program-capture/` |

## Behaviours the tests pin

| Rule | Reason |
|---|---|
| A manifest verifies only under the profile named in it | A manifest of another image must never pass these gates |
| The workspace context must name the profile's own manifest path | A product workspace is built from its own manifest repository |
| No other west project may sit in, contain or equal that path | Same overlap rule as for the M5 manifest checkout |
| The commit tag is required only when the profile says so | A product binary may not embed the platform commit tag |
| The errno role exists only when the profile names a source file | The errno evidence is specific to the M5 transaction code |
| Source snapshot follows the source repository, frozen tools follow the tool repository | The image source and the board tooling are different repositories for a product image |

## The S2 profile and module discovery

The crux-controller product repository builds an image called S2. Its facts were **measured in a local build by the crux-controller session; nothing has run on a board** and the first real freeze is the first real contact of this profile with a real ELF.

| Fact | Value in the profile |
|---|---|
| Manifest repository (west path) | `crux-controller`; the platform repository is an ordinary west project of that workspace, not the manifest |
| Dependencies | the same four pins at the same west paths as M5 (zephyr, cmsis, cmsis_6, hal_nxp), repeated as `S2_PINNED` so that an M5 change cannot move the S2 image |
| Build modules | crux-pilot, picolibc, fatfs and littlefs at exact full commits (`S2_MODULES`) |
| Commit tag in the binary | none (`commit_tag=False`); 0 hits for the 12-character prefixes of the product, platform and Zephyr commits. The identity is the image SHA-256 plus the firmware identity |
| Errno evidence | none |
| Frozen tool scripts | `record-build-provenance.py`. The product's own build script is not frozen by this profile: its name and place are unknown here |
| Firmware identity | `s2_check_image` computes it with the recorder's own functions (algorithm `crux-firmware-identity-v1`, target `mimxrt1060_evk@B//qspi`) and the manifest records it in `image`. It is recomputed at verification and compared with the recorded one; it is not pinned to a value in the profile, so the image is identified, as for M5, by the digest of the approved manifest |

**Why a module field.** M5 names its modules explicitly in the build (`-DZEPHYR_MODULES`), so the rule "another west project is recorded but never part of the image" holds. S2 uses west module discovery: its `zephyr.meta` lists crux-pilot, picolibc, cmsis, cmsis_6, fatfs, hal_nxp and littlefs as modules of the build. Module discovery builds every active west project that carries Zephyr module metadata (a `zephyr/module.yml`, or `zephyr/CMakeLists.txt` and `zephyr/Kconfig`); zephyr itself is an active project and is not in that list. The freeze records west projects and cannot see which of them carry such metadata, so a project that west lists and nobody pinned might be built into the image unseen. The profile therefore pins each discovered module exactly (`modules`, `module_paths`), and for such a profile `frozen_inputs` refuses every west project that is not pinned. For a profile with no `modules`, which includes M5, the code path is the one it had before; the tests keep an explicit pair, the same extra project accepted under M5 and refused under S2.

**What the S2 image check covers.** `s2_image.check_image` reads the BIN, the ELF and the resolved `.config`:

- the shared geometry check of the M3 images (`m3_image.check_image_bytes`: boot header, ELF sections against the BIN, gap fill);
- `-Os`, the board and flash lines, XIP, the boot header, no heap, the 4096-byte main stack, the stack guard, the network and socket sizes of the S2 build, the static IPv4 configuration (board address 192.168.77.2, mask 255.255.255.0, no gateway, through the `NET_CONFIG_*` options), the S2 workload lines (peer address and port, worker stack and thread count), and Crux Logging in deferred mode, which the M5 check forbids;
- none of the M5-only debug options is required (`INIT_STACKS`, `PRINTK_SYNC`, `CONSOLE_GETCHAR`, `CONSOLE_SUBSYS`, `NET_BUF_POOL_USAGE`), and storage, `WATCHDOG`, `SHELL`, IPv6, DHCPv4 and MCUboot stay refused;
- stack objects: `z_main_stack` 4160 bytes, `worker_stacks` 33,024 bytes, no `m5_observer`.

The stack sizes come from the crux-controller session's reading of the real ELF, which this repository has not seen. The tests check them against a **synthetic** ELF whose symbols were set to those values. If the real ELF differs, the first freeze refuses it; that is the check working or the measurement being wrong, and it is not a defect of the board path.

**Expected consequences of the first real freeze (not yet seen).** (1) A module whose revision in the product manifest is a branch or tag name is refused: the recorder keeps the manifest's revision string, and the freeze requires it to equal the resolved commit and the pin. (2) The workspace must list exactly the four dependencies and four modules; any other active project is refused, even one without module metadata. (3) The platform repository is a pinned module at the revision above, which need not contain this profile. The freeze and driver therefore run from a separate platform checkout that does (`--tool-repo` for the freeze), not from the workspace's module checkout.

**Cross-check of the fixtures.** The committed configuration (normalised the way the recorder normalises it) and the recorded image SHA-256 reproduce the recorded firmware identity `8493243091295c6d9ec305b6e5b097df738ee226550394a5ab0fdac388f73164`; a test recomputes it. The BIN itself is not committed (103,180 bytes); its hash is in `facts.json`. The real BIN was read once by hand when the facts were collected: its size, the `FCFB` header and the pinned IVT bytes match what `check_image_bytes` requires of a BIN. That is not a test and is not repeated by one.

## UART capture in `program` mode

Software only. Nothing here ran on a board, and nothing here authorises a programming, reset or any other board step: a first physical use of an S2 profile with a capture still needs its own approval, reviewed plan and the root mount guard.

### Is the product session's reading right?

Yes, for every claim that was checked. Line numbers are those of `origin/main` at `abfd1879`, before this change.

| Claim | Verdict | Where |
|---|---|---|
| Every mode acquires the exclusive UART | Right | `Driver.__enter__` calls `acquire` whenever one is given, `program_m5_image.py:92`; `acquire` takes the board lock and the exclusive port (`:323-343`) |
| `program` holds the port and reads nothing | Right | `Driver.program` (`:159-166`) only calls `run`; `self.port` is not used in it. Only `restore` reads it: `capture = observe(self.port)` (`:175`) |
| `restore` reads it through `Observation` and checks the normal image's boot banner | Right | `Observation.finish` requires `*** Booting Zephyr` and the `M3_BOOT source=...` line (`:203-205`) |
| `capture_m5_network.py` is the M5 phase protocol | Right | its `capture` subcommand needs an approved reduced or full M5 plan, `--perform-approved-reset` and `--boot`, and "never programs, restores, or switches a cable" (`capture_m5_network.py:1-7, 489-497`) |
| A reused `Receiver` stops on a cut final line, a non-ASCII byte and a poll gap | Right | `capture_m3.py:135` (`Incomplete final serial line`), `:133` (strict ASCII decode), `:118` (`Serial reader discontinuity` above 0.25 s) |
| For an image with no phase runner the boot and the first seconds would be lost | Right, with two additions found while reading | (a) `capture_m3.drain`, which `Observation` calls first, raises `Unable to drain before fresh reset` unless the line is quiet for 50 ms within 3 s (`:157-167`). A continuously logging entry image can fail that before anything is loaded, so the new capture does not drain. (b) The load, verify, reset and go are **one** Commander run (`capture_m3_network.py:226-231`), so a capture can open before that run but never between its reset and its go, and the stream holds the entry image's last output, then silence, then the new image |
| Under A1 the step's `done` marker must be written before the window | Holds by construction | `Driver.run` notes `done` when the Commander run returns (`:141`); the window runs afterwards in `program_captured` |

### Decisions

| # | Decision | Why, and the alternative that was rejected |
|---|---|---|
| 11 | The capture is a **profile property**: `capture` (`none` or `raw`) and `capture_seconds`. M5 stays `none`, S2 is `raw`. The driver is not changed for everybody: without a capture `Driver.program` runs the original lines | M5 behaviour stays byte for byte (the unchanged M5 tests pass, and a test compares commands, checks and re-verifications with and without a capture). Rejected: a driver-wide capture, which would change the reviewed M5 run |
| 12 | The gates stay in `Driver` and take nothing from the profile. `Driver` gets a `CaptureSpec(seconds, factory)`, never the profile. Tests pin the field list of `ImageProfile`, the parameter lists of `Driver.__init__` and `Driver.run`, that an unknown gate keyword is a `TypeError`, and that every registered and extreme profile produces identical commands, checks and re-verifications | A profile must not be able to relax a gate. The one setting it does carry can only add a read |
| 13 | The capture **opens inside `Driver.run`, through an optional `before` hook, after the guard check, the ownership inventory, both budget checks, the image hash check and the manifest re-verification of the load step, and before the one probe command**. It never opens for the identity-only or the entry-image step, and a refusal of any gate leaves no capture and no capture directory | The capture must not exist in front of a gate, and it must be in place before the reset. Rejected: opening it before the guard checks (it would record while a gate is still deciding), and opening it in `__enter__` (it would also run through steps that cannot start the image). The hook runs the callable after the last check and cannot skip one. It adds at most the reader start (a bounded 1 s) between the last check and the command |
| 14 | The load, verify, reset and go stay the **single** command `image_commands(binary, True)` | Splitting it into a load run and a start run would add a second Commander run, a second guard cycle and a new operation name in an approved plan. Consequence: the driver cannot tell where in the run the reset happened; the offline analysis splits the stream by the `opened` mark, the quiet periods and the content of the lines |
| 15 | The window is `capture_seconds` **after the Commander run has returned and its `done` marker is written**. It is a whole number of seconds from 1 to 300 (`MAX_CAPTURE_SECONDS`, at most about 3.5 MB at 115200 baud), part of the registered profile, and must fit the series budget: `now + 30 s (the Commander limit) + window < series end`, checked before the load. S2 registers 180 s | Open-ended capture is refused by construction. 180 s is the crux-controller plan's proposal (the workload starts only after a network init wait of up to 30 s); the owner decides the number. The window is bound to an approved manifest through the frozen tool hashes (`image_profile.py`, `prepare_m5_network.py`, the driver), as every other profile fact, when the freeze ran from the executing checkout. The M5 plan schema has no field for it, so the written approval text must name it |
| 16 | **No drain, no discard.** Every byte the port delivers from the open on is raw evidence. Bytes that were already buffered when the capture opened come first and are the entry image's | `capture_m3.drain` can raise on a noisy line, and discarding bytes is a loss. The `opened` mark and the quiet periods let the offline analysis separate old and new |
| 17 | Recorded exactly: `uart-rx.bin` (flushed per chunk), `receive.jsonl` (time, raw offset, hex), `poll.jsonl`. The line view decodes **latin-1**, which maps every byte to one character without loss, keeps ESC and control bytes, and each row carries `offset` and `length` into the raw bytes. `analysis.json` classifies and decides nothing | The raw bytes are the evidence. The accepted `Receiver` decodes strictly and strips ANSI, which loses information for an unknown image. The acceptance analysis stays an offline replay, as for `restore` |
| 18 | A cut final line, non-ASCII bytes and poll gaps are **classified, never fatal** (table below). Only a real failure stops the reader: the port raising on a read, or a file that cannot be written | The reader of an unknown continuously logging image must survive what it records. A poll gap is an observation about the reader, not a verdict on the image |
| 19 | A capture that **cannot open** stops the run before the load: nothing in flash changes. A capture that **fails afterwards** never restarts or retries the image (below) | The purpose of the capture is the evidence of the first start; an unrecorded start is not worth the risk of an unreviewed image. There is no loop in the driver, and no path leads from a capture event to a probe command |
| 20 | A start command that fails, or whose output lacks `Verify successful`, **aborts** the capture at once instead of waiting the window, and the primary error is kept. A failure of the abort is attached as `capture_failure`, as in `restore` | The run has stopped, the operator needs the port back, and a long wait changes nothing about the state. `restore` waits its window even after a failed start; the program step does not, because its window is minutes, not 15 s |
| 21 | No capture oracle and no expected output for S2 | No capture of the S2 image exists. Any expectation would be unverified; the first real capture is the input of that work |
| 22 | Labels (`program-m5`, `M5_PROGRAMMED_VERIFIED_STARTED`) and the plan shape stay as they are | Renaming touches approved plan wording; unchanged from decision 10 |
| 23 | The capture works on the port object that `acquire` returned and never opens, closes, configures or writes it, and takes no lock | Ownership, `TIOCEXCL`, the board lock and the lsof check keep one owner; `Driver.__exit__` closes the port after the capture has stopped (the capture joins its reader before it returns) |

### Where the capture sits

```mermaid
sequenceDiagram
    participant D as Driver.program
    participant G as Gates: guard, owners, voltage, budget, hashes, re-verify
    participant C as RawCapture (reads the owned port)
    participant J as J-Link Commander
    participant B as Board
    D->>G: identity-only step
    D->>G: entry-image step, verifybin, stop if it differs
    D->>G: load step checks
    G-->>D: all passed
    D->>D: window fits the series budget
    D->>C: open (mark opened, reader starts)
    D->>J: loadfile, verifybin, r, g, q (one run, once)
    J->>B: halt, program, verify, reset, go
    B-->>C: entry image output, silence, new image output
    J-->>D: run returned, identity check, done marker written
    D->>C: window (capture_seconds, host steps may run concurrently)
    C-->>D: reader stopped, files written, summary
    Note over D,C: A failure of the run aborts the capture; a failure of the capture stops the mode. Neither starts anything.
    D->>D: port closed after the capture stopped
```

### What is recorded

| File (in `<mode directory>/capture/`) | Content |
|---|---|
| `uart-rx.bin` | every byte, exactly, flushed per chunk |
| `receive.jsonl` | per chunk: `monotonic`, `offset` into the raw bytes, `hex` |
| `poll.jsonl` | per poll: `monotonic`, `gap` (the accepted reader's format) |
| `clock.json`, `config.json` | the host clock identity; window, thresholds, read size, `passive: true` |
| `marks.json` | `opened`, `window-start`, `stopped`, or `aborted` with its reason: each with `monotonic` and `raw_offset` |
| `lines.json` | timestamped rows: `offset`, `length`, `text` (latin-1, no LF, no CR before it), `first_monotonic`, `monotonic`, `complete` |
| `analysis.json` | the classification below, the SHA-256 of the raw bytes, `acceptance: none: the offline analysis of the raw bytes decides` |

| Observation | Classified as | What it does **not** do |
|---|---|---|
| Final line without a terminator | `final_line.state = cut` with its offset, length, text and time (`complete` or `none` otherwise) | stop, hide the line, or fail the capture |
| Any byte above 127 | `non_ascii`: exact count and coalesced ranges with their offsets and the first 16 bytes as hex | decode with a loss, reject a line |
| Time between two polls above 0.25 s | `poll_gaps`: count, maximum, and each with its time and raw offset | end the reader (the accepted one does) |
| Time between two chunks of 1 s or more | `quiet_periods` with the raw offset where output resumed | judge the image |
| First and last byte | `first_byte` (seconds after open), `last_byte` | |

The lists keep the first 100 entries; the counts are exact.

### If the capture fails

| Event | The capture | The driver | The image |
|---|---|---|---|
| The capture cannot open (directory exists, disk full, reader did not start) | nothing is left running | `HardwareStop('UART capture could not be opened; no probe command runs')`, `failure.json`, port closed | not loaded, flash unchanged |
| The start command fails (timeout, identity, verify) | aborted at once, files written | primary error kept, `capture_failure` set if the abort failed, no further command | as the failed command left it, state uncertain as for any failed command |
| The run lacks `Verify successful` | aborted | `HardwareStop('Complete M5 image verification absent')` as before | as left |
| The port raises during the window | reader ends, bytes so far kept, `reader_failure` recorded, the window ends early | `HardwareStop('UART capture reader failed: ...')`; `failure.json`; no `result.json`; the `done` marker of the start step is on record | running; **not restarted, no second `r`/`g`, no retry** |
| An interrupt in the window | stopped, files written | the interrupt propagates, port closed | as above |
| The reader does not stop in 1 s | reported | `HardwareStop`; the thread is a daemon, the port is closed by `__exit__` | as above |

`restore` is the comparison: its `capture.finish()` error also propagates after the start, with the start's own error preferred when both fail. The difference is only that the program capture is raw and never judges the image.

### Interaction with the Commander output, the port and the locks

| Item | Behaviour |
|---|---|
| Commander output | The Commander run writes `NN-program-m5.log` in the mode directory; the capture writes only below `capture/`. They share no file. The UART is not part of the Commander run (SWD only), so the capture adds nothing to its log and its diagnostic rules are unchanged |
| Port ownership | One owner: the `Serial` object that `acquire` returned and `preconditions` checks by `lsof`. The capture reads it from one thread and never opens a second handle. The reader starts after the last ownership check of the load step and no check runs while it is open, because the load step has no later one |
| Lock files | `evkb-<probe>.lock` is held by `acquire` for the whole mode, as before. The capture takes none |
| One use | `consume` still allows one `program` per evidence directory, and the capture directory is created with `exist_ok=False`, so a capture is never overwritten |
| Concurrent host steps | An operator may run read-only host steps (link and address observation) from another terminal during the window. None of them touches the probe, the port or the lock |

## What is not built

- **No capture oracle, no expected UART output.** (The raw capture above records; it judges nothing.) The expected output of S2 (Crux Logging lines, `net: exchange ok cycle=<n>`, failure lines with `result=-2` or `-3`) comes from the crux-controller plan and no capture of the image exists. No oracle or check of it is written; it needs a first real capture. The Ethernet link may not come up at all (static 192.168.77.2/24, no gateway, no DHCP, no MDIO correction): unknown.
- **The capture oracle stays per image.** `m5_network_oracle.py`, `capture_m5_network.py` and the M5 phase protocol are untouched.
- **Driver labels are unchanged.** The step label `program-m5` and the status `M5_PROGRAMMED_VERIFIED_STARTED` still name the M5 image, so the evidence of an S2 program step will say `m5`. S2 is the first profile that needs a neutral label, and the S2 change leaves it open: the labels appear in the plans and approvals that name operations, so renaming them is a decision for the owner and a separate change.
- **`scripts/record-build-provenance.py` is not modified.** It is protected. How it records a product repository (`--repo`) is the input of the later profile PRs.

## Decisions taken for lack of an answer

| # | Decision | Why |
|---|---|---|
| 1 | The profile registry lives in `prepare_m5_network.py` and keeps its name | Renaming the reviewed module is out of scope. Its name is a key in the recorded tool hashes (`inputs.json` `scripts`) and in the plans and evidence that cite it |
| 2 | The default profile is M5 | Existing commands, plans and approvals keep working unchanged |
| 3 | The workspace manifest context path is the profile's `manifest_path` | Inference: a west workspace names its manifest repository by path. To be checked against a real product workspace in the profile PR |
| 4 | `tool_repo` defaults to the source repository | M5 uses one repository for both |
| 5 | A profile with `modules` refuses any unpinned west project; M5 keeps accepting them | Under module discovery an unpinned project can become part of the image |
| 6 | Modules are pinned by full commit, like the dependencies, and the profile refuses a branch or tag name | The recorder records the resolved commit besides the manifest revision, so exact commits are available and a moving name is not an identity |
| 7 | The S2 pins are repeated, not shared with M5 | An M5 pin change must not move the S2 image |
| 8 | The manifest records the firmware identity in `image` | The binary has no commit tag, and the plan and approval need a stable name of the image besides its SHA-256 |
| 9 | The freeze command gains `--profile` and `--tool-repo` | Without them the S2 profile could be registered and never frozen |
| 10 | Capture, expected output and driver labels are left out | No real capture exists; the label rename touches approved plan wording. The raw capture is added later (decisions 11 to 23); the expected output and the labels stay out |

## What is untested

- Nothing in this change ran on hardware, and none of it can: the tests use synthetic images and temporary directories.
- The profile facts of the real S2 image are exercised only through its real resolved configuration. The BIN and the ELF used by the tests are synthetic layout fixtures and are never flashable. The real ELF was not available: no symbol check has run against it.
- No freeze (`prepare`) has run on a real S2 build. Its inputs file, build provenance and the product's build script are not seen here. How the recorder records the product repository (the product is the source repository, so west's listing omits it, and the platform repository appears as an ordinary project) is read from the code of the recorder, not run.
- `main` itself has no unit test, as before. The profile wiring is the small `bind_profile` helper, which is tested; that `main` passes its result to both `load_inputs` and the re-verification is by reading the code.
- `prepare(..., tool_repo=...)` does not check that the tool repository is clean or committed. The driver already requires a clean tree for itself (`require_clean_tree`); a profile PR that uses a separate tool repository must show how that repository is frozen.
- The raw capture is tested only against fakes: a real capture of stage 1 (a restore of the normal image) replayed through a fake port, and synthetic cut lines, non-ASCII bytes and poll gaps, because the real captures have none. No serial port, no Commander and no board were involved. Whether the reader keeps up at 115200 baud on the Mac, how long the kernel buffers the entry image's bytes before the capture opens, and what S2 actually prints are unknown.
- `main` passes `capture_spec(profile, args.mode)` to the driver: by reading the code and the tested helper, as for `bind_profile`.
- Real errno verification under a non-M5 profile and the exact M5 `default_tools` list are covered only through the M5 tests that were not changed.
