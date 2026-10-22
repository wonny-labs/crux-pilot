# Portable execution software evidence

Tested source: `b9df57dfefba53c938a3cddbb7f8b139b7986ab8`, based on main `32405ad`.
See [the validation report](../../execution-validation.md) for commands, results,
supported behavior and unresolved physical/#24 dependencies. Later commits in this
PR retain documentation and evidence without changing tested implementation inputs.

`$REPO`, `$WORKSPACE` and `$CACHE` replace local path prefixes in textual outputs.
Selected compiler-command records use these same placeholders. All result bodies
otherwise preserve their output; the actual source/artifact SHA-256 values were
calculated before path normalization. Full raw logs, firmware ELFs and generated
maps are also retained locally in the task's build/evidence archive. The workflows
upload fresh hosted test/build artifacts independently.

- `final-host.txt`, `final-sanitize.txt`: final sequential complete suites.
- `final-portable.txt`: separate Host consumers, both native_sim configurations,
  EVKB cross-builds and boundary checks.
- `crux.portable_execution-handler.txt`, `crux.execution.runtime_independent-handler.txt`:
  simulated test output, including stack/priority observations.
- `final-analysis-clean.txt`, `execution-analysis.txt`, `format.txt`, `dependencies.txt`,
  `build-tests.txt`: configured analysis and quality gates. Empty format output means
  the successful check emitted no diagnostics; failures are not inferred from silence.
- `final-inputs-before.json`, `final-inputs-after.json`: identical source/workspace
  captures for the final full portable script, including the clean Git identity.
- `input-hashes.json`: individual relevant source and target artifact hashes.
- `*-evkb.config`, `*-evkb.map.gz`, `*-compile-commands.json`,
  `target-resources-and-tools.txt`: generated configuration, compressed full maps,
  selected consumer/provider commands, ELF symbol allocation and compiler identity.

Negative history is deliberately retained. `poll-red.txt` demonstrates spurious
BUSY with available tokens before the poll correction. `stack-red.txt` demonstrates
the accidental Host 4096-byte request restriction before provider-specific limits.
`evkb-abi-red.txt` catches a portable consumer missing Cortex-M7/Thumb flags.
`admission-zephyr-red.txt` records the ineffective PRIVATE link options on Zephyr's
static `app`; `admission-zephyr-green.txt` records all thirteen cases after propagating
those test-only wrapper options to the image link. The subsequent final portable
script supplies the final full-suite/cross-build result.

`host-exit-timeout.txt` and `host-exit-timeout-details.txt` retain the unresolved Time
process-exit timeout; `time-exit-repeat.txt` contains twenty successful direct repeats.
A passing retry does not identify the original timeout's cause.

`posix-extra-zephyr-fixed.txt` records the additional pinned POSIX condition-variable
ownership failure, not a failing Crux provider. To reproduce the original investigation,
copy the four text files in `posix-extra/` to a scratch application directory, removing
only each final `.txt` suffix. Configure that scratch application with the pinned
Zephyr workspace and run Twister on `native_sim/native/64`. `probe.c` also builds on
Linux with `cc -std=c17 -D_GNU_SOURCE probe.c -pthread -o probe`; assertions must remain
enabled. The zero-warning negative run and independent source review support the
finding described in the report. No upstream source was modified.

Independent review: [review record](review-record.md). No physical hardware result,
production runtime compatibility, full #24 policy adoption, merge approval or Issue
#25 completion is asserted by these artifacts.
