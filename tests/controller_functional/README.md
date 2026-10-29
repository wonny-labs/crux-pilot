# Reference-controller pytest PoC — Issue #60

This is an opt-in test-tooling PoC, not a replacement for existing CTest/GoogleTest,
Twister/Ztest or Python checks, and not a mandatory CI gate. It uses the existing,
merged 20-cycle reference-controller applications without modifying Crux APIs or
physical-target procedures.

## Prerequisites

Run inside the documented Linux Dev Container with the pinned Zephyr workspace.
Use pytest 8.3.5 from the project environment. The Zephyr baseline comes from
`west.yml`, and the SDK version from `docs/build.md`. Record `git rev-parse HEAD`,
`west list zephyr -f '{revision}'`, `pytest --version`, and the toolchain version
with the actual test evidence.

## Normal runs

From the Crux repository root, first build the existing Host controller using
the standard Host checks, then execute its bounded pytest scenario:

```sh
scripts/check-host.sh
pytest -q tests/controller_functional/test_host.py \
  --junitxml=build/evidence/issue-60/host-pytest.xml
```

For Zephyr, run from the west workspace (the repository's parent directory):

```sh
repo="$PWD/crux-pilot"
west twister -j 2 -p native_sim/native/64 \
  -T "$repo/samples/controller/zephyr" \
  -O "$repo/build/zephyr/twister-controller-pytest" --inline-logs
```

Twister builds and launches the existing Zephyr sample through `harness: pytest`.
Its `dut` fixture owns the simulator process and collects output until the
success/failure marker. Zephyr may leave the kernel idle after `main()` returns,
so this test does not infer success from a simulator process exit code.

## Controlled negative runs

Use a separate run and evidence directory for each environment. Setting
`CRUX_PYTEST_BAD_OUTPUT=1` changes only the *captured test-side output*, turning
the observed valid flag at cycle 6 from `1` to `0`. Neither firmware nor public
APIs are modified. The test is **expected to fail with a cycle-6 assertion**, not
with a build, fixture or timeout error. Remove the environment variable for
ordinary runs.

```sh
CRUX_PYTEST_BAD_OUTPUT=1 pytest -q \
  tests/controller_functional/test_host.py \
  --junitxml=build/evidence/issue-60/host-negative.xml
```

From the west workspace:

```sh
repo="$PWD/crux-pilot"
CRUX_PYTEST_BAD_OUTPUT=1 west twister -j 2 -p native_sim/native/64 \
  -T "$repo/samples/controller/zephyr" \
  -O "$repo/build/zephyr/twister-controller-pytest-negative" --inline-logs
```

A nonzero return code is expected **only** when the report shows the intentional
cycle-6 mismatch. Compare the normal and negative JUnit/console/Twister reports.
Do not conceal unexpected failures by marking every nonzero code acceptable.

## Evaluation boundary

The PoC repeats a small amount of the existing `scripts/check-controller.py`
output validation to avoid refactoring a working runner solely for an experiment.
Assess that duplication, the pytest plugin dependency, process/fixture complexity,
execution time, and JUnit diagnostic quality before deciding whether to consolidate
the oracle or adopt pytest. Keep CTest/GoogleTest for unit/component tests and
Twister/Ztest for native integration. The existing `tests/target` pytest suite is
physical-only and must not be imported or executed by this PoC.

## Verified execution record — 2026-10-29

Source under test: `acd90a6384998c7de2d20b621b8252643d3c95a3` (the PoC handoff
revision). Runs used the project's Ubuntu 24.04.3 Linux Dev Container on x86_64:
Python 3.12.3, pytest 8.3.5, West 1.4.0, CMake 3.28.4, Ninja 1.11.1,
Clang 18.1.3 for the Host build, and system GCC 13.3.0 for Zephyr
`native_sim` (`host_gnu`). Zephyr was v4.4.0 at the `west.yml` pin
`684c9e8f32e4373a21098559f748f06915f950c9`. Zephyr SDK 1.0.1 was installed in
the environment; `native_sim` used the host GNU toolchain, not the ARM SDK.

| Check | Result |
| --- | --- |
| `scripts/check-host.sh` | Pass: full Host build (226 Ninja steps), CTest 169/169, and Host sample stdin/stdout scenario. |
| Host pytest, default output | Pass: 1/1; 1.97 seconds against the built Linux controller binary. |
| Host pytest, `CRUX_PYTEST_BAD_OUTPUT=1` | Expected failure: 1/1 fails at cycle 6 because observed `valid=0`; 1.98 seconds. Process exit was checked before the functional oracle. |
| Twister pytest `native_sim/native/64`, default output | Pass: 1/1 configuration and testcase, no warnings; 106.83 seconds including a fresh Zephyr build; pytest case 2.08 seconds. |
| Twister pytest `native_sim/native/64`, `CRUX_PYTEST_BAD_OUTPUT=1` | Expected functional failure: 1/1 configuration fails at cycle 6 because observed `valid=0`; no build, fixture, or timeout error; 106.85 seconds including its separate fresh build; pytest case 2.08 seconds. |

Normal and negative Twister runs used separate output directories to retain
independent reports. Their measured wall time therefore includes repeated build
cost and is not pytest execution time alone. The run used
`-O /opt/crux-cache/issue-60/twister-{normal,negative}` instead of the example
repository `build/` output paths so reports and build artifacts stayed in Docker
volumes while the Mac host filesystem had less than 700 MiB free. Host build
output was also kept in a Docker volume. No repository cache or user data was
deleted; test selection, fixture and assertions were unchanged.

The Twister negative report records `FAILED: 1/1 pytest scenario(s) failed`,
then reports the specific cycle-6 assertion. Zephyr's simulator process itself
returned 0 after the app completed; pytest correctly rejected the altered
captured observation. This distinguishes the intended functional oracle failure
from build or harness failure. Host pytest additionally requires the controller
process to exit 0.

### Adoption recommendation

**Recommendation: retain this as a bounded, opt-in PoC; do not add it as a
separate routine suite or mandatory CI gate.** The PoC confirms that pytest can
assess the same observable controller behavior in both environments and gives
clear pytest assertion traces. It does not add behavioral coverage:
`scripts/check-controller.py` already validates the 20 observations, status and
UTC behavior, monotonic progression, and completion marker, while CTest already
runs the Host scenario and Twister remains the Zephyr build/execution owner.
CTest and Twister already emit JUnit reports retained by CI, so pytest's XML is
not a distinct reporting benefit here. Pytest 8.3.5 is already pinned in the
Dev Container and physical-target requirements; the incremental cost is the
additional entrypoint, fixture and copied oracle, which can drift from the
existing runner. The measured fresh Twister build takes about 107 seconds
while the pytest case itself takes about two seconds; that timing does not
isolate or demonstrate a pytest overhead advantage. Revisit pytest for routine
use only if a concrete integration workflow benefits from its fixtures,
parametrization or failure diagnostics beyond the reports CTest and Twister
already produce. Consolidate the shared assertions and integrate with an
existing execution path before adding a parallel rebuild and regression suite.
Keep unit/component coverage on CTest/GoogleTest and Zephyr integration coverage
on Twister/Ztest.

This recommendation concerns tooling only. These runs do not qualify physical
hardware, other Zephyr boards, long-duration behavior, or M7 acceptance.
The oracle parses the scenario's lateness field but does not assert a lateness
bound, matching the existing runner; it adds no timing guarantee.
