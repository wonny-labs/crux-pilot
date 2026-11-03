# Build and debug tooling scope: proposal

Status: **section 5 approved and implemented on this branch; section 6 undecided.** The owner approved the change draft of section 5 on 2026-11-03 ("추천 범위로 시작", Claude desktop session, about 23:40 UTC), including the two protected files `scripts/build-debug-target.sh` and `scripts/debug-optimizations.conf`. It is implemented in the pull request that carries this document. Nothing in section 6 is adopted: options A, B and C for a SEMC J-Link script, the launcher and any such script are still the owner's decision. No probe, board or J-Link tool was run.

Bases: Crux `origin/main` 82d2c9b. Zephyr `dccb09599635` (v4.4.2), read with `git show` because the local checkout is at v4.4.0. J-Link software V9.86a for the library check in section 6. Line numbers for Zephyr files refer to that revision.

Labels: **Read** means the source says so. **Inference** means reasoning that no build or run has shown. **Not run** means a check that was not made.

## 1. Question and answer

The owner asked whether the platform must provide J-Link scripts and build or debug helper scripts.

- A general wrapper over `west flash` or `west debug` is not needed. The runner cannot replace the current flow here (section 3), and `AGENTS.md` asks to reuse existing tools and to approve hardware operations per task.
- The platform still has debug assets that it should state and keep: the GDB Server launcher, the VS Code profiles and the debug build script. Today they cover one application (Time) and one flow (QSPI download).
- Whether the platform also ships a board-level SEMC initialisation script for J-Link is an owner decision (section 6). No repository or upstream file does it.

## 2. Current flow

| Part | What it does | Limit |
|---|---|---|
| `scripts/macos/start-jlink-gdbserver.command` | Starts the J-Link GDB Server on the Mac for `MIMXRT1062xxx6A`, SWD at 400 kHz, port 2331, remote connections allowed | The text says 400 kHz was verified for QSPI download and a `main` breakpoint. No script file is passed. |
| `.vscode/launch.json` | **Flash** builds, then runs `monitor reset` and `-target-download` through `host.docker.internal:2331`. **Attach** loads symbols for an ELF and connects without download | Flash names the Time ELF. Both assume the code is in QSPI. |
| `.vscode/tasks.json` | **Build firmware** runs `scripts/build-debug-target.sh 'mimxrt1060_evk@B//qspi'`. **Clean firmware** cleans that one directory | Fixed board target and directory |
| `scripts/build-debug-target.sh` | Incremental build of `samples/time` into `build/zephyr/<board>`, then prints the ELF hash. Deletes `provenance.json` | The application is fixed. No optimisation choice. |
| `docs/build.md`, "RT1060-EVKB debugging" | Says it is not a general programming procedure and asks for task-specific approval | Names the Time sample only |

The container builds and holds GDB. The Mac holds the probe. No script calls `west flash` or `west debug`.

## 3. What the Zephyr J-Link runner can replace

Read at `dccb09599635`, `scripts/west_commands/runners/jlink.py` unless noted.

| Runner behaviour | Evidence | Effect on this repository |
|---|---|---|
| Needs a local J-Link Commander for every command, including `--gdb-host` | `:327-328` requires it before the command branch. `:330` reads the version from the library beside it (`:283-285`). | The Dev Container has no J-Link (`.devcontainer` has no reference), so `west debug` or `west attach` cannot reach the Mac server from there. Installing SEGGER software into the image would add a proprietary dependency. |
| `debugserver` cannot be used with a remote host | `:381-382` | It would have to run where the probe is, with the build tree, west and `runners.yaml` of the container |
| Server flags differ from the launcher | `:362-376`: `-singlerun`, `-select`, speed `auto`, no `-localhostonly`. The launcher sets 400 kHz and `-localhostonly 0` | The launcher's choices are the verified ones. The runner's defaults are not. |
| `west debug` resets, then loads | `:446-448`: `monitor halt`, `monitor reset`, `load`, always | Same sequence as the Flash profile, so nothing is gained for QSPI. The reset comes before the load. |
| `west flash` | Runs `ExitOnError 1` (`:474`), then `r` (`:475`), with optional `RSetType` and pre-script commands ahead of both. Erase is opt-in: `--erase` has no default (`runners/core.py`) | Not used by any Crux script. A test already asserts that the GPIO runner does not call it. |
| `--flash-sram` | `:544-546` sets the PC to the SRAM base address after loading | **Inference:** for an image whose vector table starts at that base, the PC would point at data. It is not a way to start code in SDRAM. |
| Script hook | `--tool-opt` is added to the server (`:375`) and to Commander (`:604`). RT1180 uses it: `board.cmake:13,18` pass `-jlinkscriptfile` | A script is possible, but it is data the board must own. |
| Board data for RT1060 | `board.cmake:14` device `xxx6A` for every revision. `:25` QSPI loader | The `xxx6A` name has no recorded problem. The repository's physical runs used it with verified programming. No action. |

Result: the runner can replace the **Flash** profile only on a machine that has J-Link, west, the build tree and the toolchain together. That is not the repository's flow. It cannot replace the container-to-Mac split.

## 4. Proposed policy

1. **Platform provides**, for the verified RT1060-EVKB QSPI target: the GDB Server launcher, the VS Code profiles, a debug build script that takes the application, an optional `-Og` fragment, and the documentation of what they do and do not cover.
2. **Project owns**: the board, including SDRAM timing and the DCD; any J-Link script or flow for a board other than the verified one; its own build, flash and debug scripts, which may reuse the platform's pieces.
3. **Not provided**: a general flash or debug wrapper, unattended programming, and (until decided in section 6) a SEMC initialisation script.
4. Every physical operation keeps task-specific approval, as `docs/build.md` and `AGENTS.md` already require. A build with the `-Og` fragment is never verification evidence.

## 5. Change draft

The patch touches five files (section 7 lists the protected ones). It was applied to a scratch clone, never to this tree.

- `scripts/build-debug-target.sh <board> [--app <dir>] [--debug-optimizations]`. The default is unchanged: Time at `-Os` in `build/zephyr/<board>`. `--app` checks that the directory has a `CMakeLists.txt`. Without either option the directory is unchanged. Otherwise it is `build/zephyr/<board>[--<application path>][--og]`: the application part appears whenever `--app` is given, even for `samples/time`, and `--og` whenever `--debug-optimizations` is given. The directories differ because the script passes configuration arguments only when it creates a build directory, so reusing one could keep a stale choice. An application outside the repository would be keyed by its full sanitised path (not tested).
- `scripts/debug-optimizations.conf` holds one line, `CONFIG_DEBUG_OPTIMIZATIONS=y`. **Read:** that is the `-Og` member of the optimisation choice (`Kconfig.zephyr:541-545`). `CONFIG_DEBUG=y` would default to it too (`Kconfig.zephyr:515`), but it is a broader switch that was not examined, so it is not used.
- `.vscode/tasks.json` gains a build task that asks for the application. Neither profile in `launch.json` changes. **Flash stays Time only.** A profile that downloads any application is not proposed: it would turn one keystroke into an unapproved physical operation.
- `docs/build.md` gains the usage, the directory rule, the "not evidence" statement, the limit that `--app` builds an application's base configuration only (the controller and networking compositions need extra files, overlays or modules and keep their own scripts) and the three-way table of section 4. It also renames the link text "Time sample build task", which would be out of date.
- `tests/build/test_debug_target.py` runs the script with a stand-in `west` and checks the default arguments and directory, `--app`, the fragment, their combination, that an existing directory is not reconfigured, rejected arguments and the fragment content.

Risks and limits:

- A `-Og` build may change stack use and timing. **Not run:** no build used the fragment, and no stack was measured. The image checks in `tests/target/` pin layout, boot header and stack sizes, but **not** the optimisation level (`m3_image.py:97-102`, `m5_image.py:8-23`), so a `-Og` image with matching settings would pass them. What keeps debug builds apart is the separate directory, the missing provenance and the "not evidence" statement. The draft changes nothing in `tests/target/`.
- The **Clean firmware** task still cleans the default directory only.
- A debug build records no provenance. Tooling that requires provenance rejects it; nothing else stops it being used as an image.

## 6. Decision for the owner: a SEMC initialisation script for J-Link

### Why it is a decision

A script is needed only to load code into SDRAM from the debugger. No Crux target does that today: code runs from QSPI, the ROM runs the DCD on a boot from QSPI, and the image checks refuse an SDRAM image. **Not verified:** whether a J-Link reset lets the ROM run before the core halts. So the question arises only if such a configuration is adopted for the EVKB. If it is not, choose option A.

Two runner facts shape any script:

- `west debug` resets **before** `load` (`:446-448`). SEMC must be ready after that reset, not only before it. SEGGER's script hooks `SetupTarget` and `AfterResetTarget` exist in the V9.86a library (checked in its strings). **Not verified here:** exactly when J-Link calls each of them. SEGGER's knowledge base describes them and must be read first.
- `--flash-sram` is not a substitute (section 3).

### Options

| Option | What it means | Cost |
|---|---|---|
| A. No script | The platform documents QSPI download only. A project that runs from SDRAM writes its own. | None. A project may need to rediscover SEMC setup. |
| B. Platform ships one for the EVKB | A script file in this repository, passed with `-jlinkscriptfile` (the launcher and `--tool-opt`, as in the RT1180 `board.cmake`) | Platform owns register values and their review. Needs the check and licence below. |
| C. Platform documents only the hook | The launcher takes an optional script path. The platform ships no content. | Smallest code. Each project owns the content. |

### Keeping it consistent with the DCD (for B)

The EVKB DCD is a byte array of address and value pairs in the board's `dcd.c`, applied by the ROM. A script that repeats part of it can drift when Zephyr's file changes at a pin update. Options:

1. A test that decodes `dcd_data` at the pinned revision and compares the SEMC, clock and pad writes with the script. Needs the Zephyr tree, so it runs where the west workspace exists.
2. Generate the script from the DCD and check that the committed copy equals a fresh generation. One source of truth, one more generated file.
3. A reviewed comparison table in the documentation, with no automated check. Cheap, and it drifts silently.

The DCD also sets clock gates, PLL and dividers, not SEMC alone. A script must say which of these it repeats.

### Licence (for B)

The repository is Apache-2.0. Zephyr's RT1060 `dcd.c` and RT1180 script carry "NXP, SPDX Apache-2.0" headers (read). The one SEGGER sample script read (`iMX23.JLinkScript` in the installation) carries a SEGGER copyright header and no licence grant, and SEGGER's licence text restricts redistribution of its software, so SEGGER samples must not be copied. The options: write the script from the DCD values with the NXP notice kept; or keep any vendor script outside the repository.

### Recommendation

A, unless an SDRAM-resident EVKB configuration is adopted. If one is, C first, then B with check 1.

## 7. Protected paths and approvals

Paths changed by the draft, with respect to `AGENTS.md` ("Protected paths"):

| Path | Change | Protected | Approval needed |
|---|---|---|---|
| `scripts/build-debug-target.sh` | modify | yes (`scripts/`) | explicit human approval for this change, then a material independent review |
| `scripts/debug-optimizations.conf` | new | yes (`scripts/`) | same approval. It is a configuration fragment, not a check script. |
| `docs/build.md` | modify | no | none beyond the usual review |
| `.vscode/tasks.json` | modify | no | none beyond the usual review |
| `tests/build/test_debug_target.py` | new | no | none |

Not changed: `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `docs/baseline.md`, `docs/evidence/`, `scripts/macos/start-jlink-gdbserver.command`, `.vscode/launch.json`, `tests/target/`.

If the owner chooses B or C in section 6, the launcher (`scripts/macos/`, protected) and a new script file (location to decide) would join this list, each needing its own approval.

## 8. Checks

Run on a scratch clone at 82d2c9b with the draft applied, on macOS with `uname` and `sha256sum` stand-ins because the scripts refuse non-Linux:

- `bash -n scripts/build-debug-target.sh`: passes.
- `tests/build/test_debug_target.py`: seven tests cover the default, `--app`, `--debug-optimizations`, their combination, no reconfiguration of an existing directory, rejected arguments and the fragment content. Six passed unchanged on macOS. The seventh, on an existing directory, fails there only because macOS bash 3.2 rejects an empty array under `set -u`, a pattern the original script already has; it passed with a temporary bash-3.2-safe expansion that is not in the draft, and Linux bash does not have the problem. Against the original script the tests for `--app` and `--debug-optimizations` fail (checked with the first five). The tests were written after the script, not before; the approved change should write them first or record why that was impractical. Not tested: an application outside the repository.
- `tests/build/test_provenance.py`: the `build-debug-target.sh` case passes with the draft. Three other failures there are identical on an unmodified tree on macOS (path and `/bin/true` assumptions), so they say nothing about the draft.
- `tasks.json` parses as JSON.

Not run: a firmware build with the fragment, any Linux CI, `scripts/check_docs.py` (it needs the pinned lychee, which needs a download; Python 3.11 was available), the full `tests/build` set, and every physical operation.
