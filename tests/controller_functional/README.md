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

Actual Host and `native_sim` passing evidence, paired deliberate oracle failures,
recorded versions/commands, and an independent review are required before making
an adoption recommendation. A successful simulator test is not hardware or M7 MVP
qualification.
