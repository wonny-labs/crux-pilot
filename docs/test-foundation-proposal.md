# Test foundation for Crux consumer applications

Status: **decisions accepted by the owner on 3 November 2026; see [Decisions](#decisions)**
([Issue #169](https://github.com/wonny-labs/crux-pilot/issues/169)). The boundary below is adopted.
The [consumer test guide](build.md#testing-a-consumer-application), the maintained
[out-of-tree consumer](../tests/consumer_poc/README.md) and the
[device-control tool](../tools/device_control/README.md) implement steps 2 to 4 of the
[recommended order](#recommended-order). No milestone, CI job, runtime code or public API is added.

Principle: **applications own their tests; Crux makes those tests easy to build, run and
automate.** Test tooling stays outside the platform runtime: this proposal adds no `crux::` target,
Kconfig symbol or public header for testing, and a production image links nothing new. The
existing `CONFIG_CRUX_TIME_TESTING` is private to Crux's own tests and is not a consumer interface.

Evidence: [out-of-tree consumer](../tests/consumer_poc/README.md) and its
[recorded runs](evidence/test-foundation-poc/README.md).

## What Crux provides and what applications own

| Level | Crux provides | The application owns | Supported entry point today |
| --- | --- | --- | --- |
| Unit | Nothing is required. Portable logic can avoid Crux entirely behind its own port | Tests, framework and doubles | The application's own CTest |
| Host integration | Root `add_subdirectory()`, explicit provider selection, `crux_assert_portable_consumer()` | Scenarios, timing tolerances, the final executable's composition | [External Linux application](build.md#external-linux-application-using-crux-source) |
| `native_sim` | Zephyr module, Kconfig, the pinned revision in `west.yml` | Ztest applications, `testcase.yaml`, Devicetree and Kconfig choices | `west build` or `west twister` with `EXTRA_ZEPHYR_MODULES` in a west workspace. Listing Crux in the application's manifest is the proposed packaging route; it is not documented and was not run |
| Functional | Guidance only, plus the Zephyr revision that fixes the harness version | Oracle, console protocol, scenarios | Upstream `pytest-twister-harness`, standalone |
| Hardware | The internal [device-control tool](../tools/device_control/README.md), which has not run on hardware | Board profile, fixtures and peers, oracle, the approval to operate, evidence retention | None. `tests/target` is Crux's own suite, not a consumer interface |

Limits at every level: `native_sim` is not hardware evidence, a passing level implies nothing
about another level, and nothing here qualifies a physical board.

## Test doubles

- **Crux ships no mock per API and no public fake.** [The messaging contract](messaging-contract.md)
  keeps private fakes out of the public API, and `CONTRIBUTING.md` keeps test controls out of
  public headers. This proposal does not change either.
- **An application writes doubles at its own port boundary**, around what it needs, not around
  Crux's surface. The PoC's scripted clock is about a dozen lines and its unit test links no Crux target.
- **Crux documents controlled providers selectively**, where a real provider is impractical
  and more than one consumer benefits: `CRUX_TIME_PROVIDER=memory` and the I2C and SPI Host
  loopback fixtures exist today. A new one needs a concrete consumer and its own decision.
- Real providers are still tested with real waits: a fake alone cannot prove native behaviour.

## Device-control contract

A small Python interface for test tooling on the machine that holds the probe and UART,
implemented in [`tools/device_control`](../tools/device_control/README.md) and tested only with
fake transports. It generalises what `tests/target/conftest.py` does for one board: there, the
J-Link path, MCU, board, ready marker and POSIX-only locking are hardcoded. UART, J-Link and hash
handling are also spread across about ten other files in that directory.

| Operation | Rule |
| --- | --- |
| Board profile | Data, not code: board ID, MCU, probe serial, tool paths and commands, port, baud, DTR/RTS state, ready marker, timeouts. No path or board is hardcoded |
| Console | `open`, `read_until(pattern, timeout)`, `write`. Exclusive open. DTR and RTS follow the profile, because pyserial asserts them by default and that can reset some boards. Keeps raw bytes |
| Reset | An explicit `reset()`, off by default, enabled with `--allow-reset`. Bounded timeout, then waits for the ready marker. Never implies erase or programming |
| Flash | An explicit `flash()`, never performed by a fixture or on launch. Refused unless `--allow-flash=<approval reference>` is given, the image hash matches the manifest and the board matches the profile. Requires a verify step. No erase or fuse operation |
| Image identity | SHA-256 of each artefact checked against a manifest before any operation. Where the application prints a build identity, it is compared with the manifest after boot |
| Evidence | A new directory per run that fails if it exists. Raw RX bytes, timestamped TX/RX events, inputs, tool logs and identity are retained, including on failure |
| Single-board lock | One OS-level lock per board ID for the whole run, released by the OS if the process dies. Held by another run: fail at once, do not wait |
| Portability | No `fcntl` or `termios` at import. Lock and port details are chosen at run time. Timeouts use `time.monotonic()` |

The flags are a tool gate, not authority. [`AGENTS.md`](../AGENTS.md) still requires explicit
authorisation of hardware operations for the current task, and the approval reference only
records it. Applications supply oracles and ready markers; the tool knows no application
protocol and no product.

The implementation settles these points that the table leaves open:

- The profile separates `board_id`, one physical board and the lock's identity, from `board`, the build target compared with the manifest. Two boards of one type need separate locks.
- The profile sets an absolute `lock_directory`. A temporary directory can differ between runs, which would silently defeat the lock.
- DTR, RTS and the console timeouts have no defaults.
- Every run needs a manifest, even for a reset, so its evidence is tied to an exact image.
- The verify step is a separate profile command. `flash()` does not reset or wait for boot.
- `flash` and `verify` commands must name `{image}`, so they act on the file that was hashed. Rendered arguments, without the image path, are rejected if they contain `erase` or `fuse`; the contents of a script or command file are not checked.

## Build and flash host split

A consumer may build in a Linux container and keep tools, probe and UART on a separate machine,
possibly Windows. So the contract assumes no shared filesystem, path layout or operating system:

1. The build host produces a hand-off directory: images plus a manifest with source and Zephyr
   revisions, board and SHA-256 per file. `scripts/record-build-provenance.py` already records
   this kind of data for Crux's own images and is the starting point.
2. The tool host verifies hashes, takes the lock, performs only the operations it was
   explicitly allowed, and writes evidence. It builds nothing.
3. Evidence returns with its manifest, so the result stays tied to an exact image.

`west flash` is not assumed on the tool host (see the findings below). Pytest runs on the tool
host; see [decision 4](#decisions).

## Packaging

- A plain Python package at `tools/device_control`, consumed through the Crux revision the
  application pins in its `west.yml`. A tool host without west uses an archive of the same
  revision, named in the manifest. **No PyPI package** and no version promise.
- **Internal until a second consumer uses it.** Until then it can change without compatibility
  work, and Crux's own `tests/target` stays as it is.
- The harness comes from the pinned Zephyr checkout, so its version moves only with `west.yml`.

## `pytest-twister-harness` findings

Standalone use **ran on Linux** at the pinned Zephyr, with no Twister and no west process at test
time: `pytest --twister-harness --device-type=native --build-dir=... -p twister_harness.plugin`.
Upstream marks it Alpha, version 0.0.1.

**Executed: only the Linux import chain and the `native` device, marked "ran" below. Every other
entry in this section is source reading, not execution.** No macOS, Windows, serial or hardware
path was run.

| Area | Linux | macOS | Windows |
| --- | --- | --- | --- |
| Declared in `setup.cfg` | Yes | Not listed | Yes |
| Import chain | Ran | Nothing Linux-specific found | `pty` is guarded by `os.name`; pyserial selects its Windows backend |
| Import requirements | Ran on Linux, read for the others: `ZEPHYR_BASE` must be set and four Zephyr files load (`twisterlib` `__init__`, `hardwaredata`, `harnessconfig`, `build_helpers/domains`). `pyyaml` and `pykwalify` are needed but not declared. Applies to all three | Same | Same, so a tool host needs that part of a Zephyr checkout |
| `native` device | Ran | `native_sim` is a Linux process, so not applicable | Not applicable |
| `qemu` device | Uses `os.mkfifo` | Available | `os.mkfifo` does not exist; unusable |
| `hardware` serial | pyserial | pyserial, `/dev/cu.*` | pyserial, COM ports |
| `--device-serial-pty` | `pty.openpty` | `pty.openpty` | Raises `NameError`; unusable |
| `--pre-script` and similar | `Popen(path)` without a shell | Same | Same; a `.py` script will not start on its own |
| Stopping a process | psutil, then `os.kill(SIGTERM)` | Same | `os.kill` with `SIGTERM` ends the process abruptly (Python documentation), with no clean shutdown |

Gaps against the contract above, also from reading:

| Contract item | What the harness does |
| --- | --- |
| Flash gating | **`--device-type=hardware` flashes on every launch**: `dut` calls `launch()`, which always runs `_flash_and_run()`. This conflicts with what `tests/README.md` states for the existing physical suite, which never flashes. `unlaunched_dut` avoids launch but leaves connection to the test |
| Reset | `DeviceAdapter` has no reset. `--pre-script` runs only as part of the flash step. `MCUmgr.reset_device()` exists but needs MCUmgr in the image |
| Image identity | None. The PoC adds a hash in its own `conftest.py` |
| Evidence | `handler.log` and `device.log` are appended in the build directory, so runs are not separated. Output is decoded with `errors='replace'`, stripped and split into lines, so raw bytes are lost |
| Single-board lock | None. It opens pyserial without `exclusive`, so POSIX has no lock. Windows opens are exclusive through `CreateFile` share mode 0 in pyserial |
| Split hosts | Default flashing is `west flash --build-dir`, which needs west plus the build directory's `CMakeCache.txt` and `zephyr/runners.yaml`. `--flash-command` is a seam, but it receives `--build-dir` |
| Options | `--flash-before` uses `type=bool`, so any non-empty value including `False` enables it. Read timeouts use `time.time()`, so a wall-clock step changes them |
| Extension | `--device-type` choices are fixed, and `register_device_class` ignores an existing name, so the registration API cannot replace `hardware`. A conftest could override the `device_object` fixture (reasoned, not run); that is then a separate adapter outside the harness's own type |

**Assessment:** use the harness as is for `native_sim`. Do not use its `hardware` type behind a
safety claim. A hardware tool can keep the same small reading surface (`readlines_until`,
`write`), so test code moves between `native_sim` and a board with little change.

## Other PoC findings

- **`EXTRA_ZEPHYR_MODULES` is skipped when there is no west workspace.** Zephyr clears `WEST`
  when `west topdir` fails, and `zephyr_module.cmake` processes modules only under
  `if(WEST OR ZEPHYR_MODULES)`. With only `ZEPHYR_BASE` set, the failure is a missing
  `kconfig_module_dirs.cmake`, then undefined `CRUX_*` symbols, which does not point at the cause.
  `ZEPHYR_MODULES` configures successfully there. The
  [consumer test guide](build.md#testing-a-consumer-application) now warns about this.
- A consumer's own west workspace that does not list Crux works with `EXTRA_ZEPHYR_MODULES`. In
  the Dev Container layout west already finds Crux, so the variable is redundant there. Only the
  consumer workspace proves it matters.
- The module name follows the checkout directory (`crux-pilot`, `crux-src`) because
  `zephyr/module.yml` sets no `name`. A consumer must not depend on `ZEPHYR_<NAME>_*` variables.

## Recommended order

Steps 1 to 4 are approved and done, within the limits below. Step 5 is not started and needs its
own approval.

1. Done: decide the boundary and the position on doubles (decisions 1 and 2).
2. Done: add a short consumer test guide to `docs/build.md` and `tests/README.md`, with the
   no-workspace warning above.
3. Done: promote the PoC to a maintained check, as `tests/build/test_external_application.py` does
   for the external fixture. No workflow file changed. Its Host cases run wherever `tests/build` is
   discovered, which includes Host CI. Its Zephyr levels are opt-in, and CI for them is a separate
   decision.
4. Done for Linux and macOS: implement the device-control tool with fake transports and no hardware,
   and test locking and evidence. **Windows was not run**; see the
   [tool's README](../tools/device_control/README.md#tests-and-what-they-do-not-show).
5. Not started: use it from the first real consumer. Only after that, consider moving `tests/target`
   onto it, with a separate hardware approval. Migration is excluded from this work.

## Decisions

The owner accepted every recommendation on 3 November 2026.

| # | Decision | Outcome | Alternative not taken |
| --- | --- | --- | --- |
| 1 | Adopt the per-level boundary above? | Decided: yes | Later steps need a new boundary first |
| 2 | Keep "no mock per API, doubles at the application's port, selective controlled providers"? The I2C and SPI Host loopback fixtures count as controlled providers, not public fakes | Decided: yes | Public fakes would need a change to the messaging position and API review for each |
| 3 | Is standalone `pytest-twister-harness` the recommended functional entry point, given Alpha status and no version promise? | Decided: yes for `native_sim`, documented as upstream behaviour pinned by `west.yml` | Crux writes and maintains its own process runner |
| 4 | Where does pytest run with split hosts: on the tool host, or on the build host with a remote agent? | Decided: on the tool host; one process, no network protocol to secure | A remote protocol becomes security-sensitive design |
| 5 | Hardware: a small Crux tool, the harness `hardware` type with a no-op `--flash-command`, or a Crux adapter behind an overridden `device_object` fixture? | Decided: a small tool, because gating, evidence and locking would all sit outside the harness anyway. The fixture override is the fallback if keeping `dut` matters | The no-op option accepts the gaps above, with gates only by convention |
| 6 | Tool location and name, and "internal until a second consumer"? Should the path be protected? | Decided: a new top-level `tools/` path, outside runtime CMake, added to the `AGENTS.md` protected paths because it holds a hardware safety gate | Unprotected, the gate can be changed without human approval |
| 7 | Is a reset gate needed separately from flash? | Decided: yes; `AGENTS.md` treats reset as a hardware operation | One combined gate |
| 8 | Keep `tests/consumer_poc` after this decision? | Decided: keep it, because step 3 is approved | It stays unmaintained and drifts |

The objection from [#60](https://github.com/wonny-labs/crux-pilot/issues/60) was no recurring
consumer and a duplicated oracle. Here the oracle belongs to the application, so nothing is
duplicated, but the recurring consumer is still only expected. Step 5 tests that assumption.
