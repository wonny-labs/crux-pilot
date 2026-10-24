# Candidate verification record

Baseline: accepted software PR #56 `b31f4f0`. New candidates are not production
adoption. Actual native-engine and ARM candidate identities are in their reports.

- Parent image/request/linker gate: 6/6 Linux tests PASS. Missing fences and
  mismatch checks were first observed failing; the linker test uses the exact
  sample fragment at the ceiling and one byte beyond it. Five cross-platform
  checks also passed on macOS; the GNU linker case is Linux-only.
- `python3 -m unittest discover -s tests/build -v`: 302 test methods executed in
  the Linux container; 301 passed. `test_dependencies.DependencyTest.
  test_portable_controller_rejects_native_coupling` produced five errors (four
  injected subcases and cleanup), because it writes temporary sources into the
  read-only repository mount and its cleanup could not follow the host worktree
  Git path. These were environment errors, not accepted passing results.
- The affected dependency test was rerun on the writable macOS worktree using
  `python3 -m unittest discover -s tests/build -p test_dependencies.py -v`: 1/1
  PASS. No test or production source was changed to hide the mount limitation.
  Full initial log and rerun are retained outside the repository; no full green
  Linux302/302 claim is made.
- Existing portable Host debug/sanitizer and File/Configuration software evidence
  from PR #56 remains applicable: this branch does not change those sources.
  Do not represent that inherited evidence as new test execution here.

New native provider/candidate gates remain focused documented manual checks;
standard hosted CI cannot be assumed to execute them. Re-run affected gates and
review if the candidate inputs, pin or configuration changes. No hardware or
programmer operation was run. Red logs retain intended failures, not regressions
silently counted as green. The parent RED log normalizes the repository path to
`/repo`; the full unmodified log is retained outside the public repository.

Final whole-repository format and dependency checks passed with the explicit
cached formatter and container Git/worktree mapping. Parent independently checked
all14 Flash owned inputs and20 Settings fixture inputs against retained hashes,
and hashed retained final ELF/BIN matching the proposal. Tracked Zephyr/NXP
sources remain unchanged; unrelated untracked Finder metadata was left untouched.
