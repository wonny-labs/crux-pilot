# Out-of-tree consumer (Issue #169)

Status: a maintained example of the [consumer test guide](../../docs/build.md#testing-a-consumer-application),
not a supported interface. `tests/build/test_consumer_poc.py` keeps it working; see the
[test guide](../README.md#testing-a-consumer-application) for what runs where. The
[test foundation proposal](../../docs/test-foundation-proposal.md) records the decisions and the
[recorded runs](../../docs/evidence/test-foundation-poc/README.md) the original evidence.

This directory stands for an application that lives elsewhere, the same way
`tests/external_application` does: the check and `run_poc.sh` copy it outside the Crux tree before
building it. The application uses public Crux capabilities and owns all of its tests.

| Level | Files | Runs through |
| --- | --- | --- |
| Unit, with the application's own scripted clock | `host/test_periodic.c` | CTest; links no Crux target |
| Host integration, real Linux clock and sleep | `host/test_integration.c` | CTest; root `add_subdirectory()` |
| `native_sim` Ztest | `zephyr_test/` | `west twister` with `EXTRA_ZEPHYR_MODULES` |
| Functional, console oracle | `zephyr/`, `functional/` | `west build`, then pytest with `pytest-twister-harness` and no Twister |

The periodic policy follows `samples/messaging/controller.c`. That sample keeps the policy
as a static function beside its `main()`, so this consumer restates it behind a clock port
instead of compiling the sample.

`-DCONSUMER_DEFECT_REBASE=ON` builds a real policy defect. The functional oracle must fail on
that image with `cycle 3: skipped 0, expected 2`; a build, fixture or timeout error does not
count, and `run_poc.sh` checks the JUnit report for this. The unit and Host integration tests
must also fail on it, which `tests/build/test_consumer_poc.py` checks.

Run it in the Linux development environment, from a west workspace, with a new output
directory outside the Crux tree:

```sh
ZEPHYR_BASE=<workspace>/zephyr tests/consumer_poc/run_poc.sh <new-output-dir>
```

The script never flashes, resets or opens a device. `native_sim` results are not hardware
evidence, and nothing here qualifies a physical board.
