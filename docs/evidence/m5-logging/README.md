# M5 Logging evidence

These are actual local development results, committed alongside the implementation.
`$REPO` and `$WORKSPACE` replace local checkout/workspace paths; they are not
literal paths required by a tool. The baseline is `main` `6a58145`, design approval
`ed6f3bd`, followed by common/Linux commit `1b266d8` and the native/consumer changes
in this PR. The final PR records the exact final HEAD and hosted check results.

Supported reproduction in the documented Linux Dev Container/pinned west workspace:

```sh
scripts/check-host.sh
CRUX_HOST_PRESET=host-sanitize scripts/check-host.sh
scripts/test-logging.sh
scripts/check-format.sh
scripts/analyze-host.sh
scripts/check-dependencies.sh
```

During development, focused Host targets and `ctest -R Logging` were used instead
of repeatedly running unrelated suites. `test-logging.sh` builds actual public
consumers, executes the explicit configuration matrix and real UART-emulator
native tests, and cross-builds EVKB revision B. It performs no hardware operation.
The format/native guard checks use actual resolved configuration and compile
failures with intended diagnostics, not fake headers or a provider substitute.

- `*-red.txt`: expected failures before a capability/guard existed. In the
  configuration RED logs, assertion `0 == 0` means the unsupported composition
  incorrectly built successfully. GREEN then requires the intended rejection.
- `composition-initial.txt`: initial 15-case native matrix, before later affected
  additions. Subsequent files record changed/added cases; final hosted CI runs
  the complete current matrix. Do not reinterpret an earlier run as a later HEAD.
- `native-sim.txt`: actual selected native UART backend with delayed copied data,
  all severities, max body, native drop report, ISR rejection, independent
  consumers and 3×3 complete records in each producer's order. No total order
  across producers is asserted.
- `evkb.txt` and `evkb-observations.txt`: actual revision-B cross-build and static
  resource/configuration observations. These are not board runtime measurements.
- `quality.txt`, `native-boundary.txt`, `consumer-boundary.txt`: executed owned-code
  formatting, analysis/dependency checks and source/usage boundary regressions.

Native queue/backend tests establish representative Pilot behaviour, not reliable
log delivery, exhaustive stress coverage or a whole-call worst-case bound. Earlier
local process-exit delays remain unresolved; successful runs do not establish
causality or a fix. Independent assessments are recorded in `reviews.md`.
