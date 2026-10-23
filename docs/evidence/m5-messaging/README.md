# M5 Issue #26 evidence

Base: main `225cd21b5fff37c31c5dff52570e9f26179682a1`. The PR commit contains
this source snapshot; `source-sha256.txt` identifies all 64 implementation,
consumer, test and integration inputs. Logs were captured from that uncommitted
snapshot before its final commit; they are not falsely labelled as commit runs.
GitHub checks must separately match the PR's final HEAD. Machine-specific roots
are replaced with `$REPO`, `$WORKSPACE`, `$CACHE` and related placeholders;
trailing whitespace and surplus terminal blank lines are normalized.

- `host-debug.txt` and `sanitize-unassisted.txt`: standard Linux scripts, 86/86
  plus sample. `sanitize-*-failure.txt`/`*-lasttest.txt`: retained earlier failures.
- `harness-zombie.json`: one separate invocation had a zombie child with exit0
  while CTest waited. TERM did not recover the parent; KILL ended the invocation
  (outer session exit247). Never count this invocation as passing.
- `sanitize-default-timeout-diagnostic.txt`: additional CTest default20seconds
  for otherwise unbounded tests, retaining explicit limits; supplementary86/86.
  Standard unmodified sanitizer success is the qualification result.
- `queue-basic-{red,green}.txt`: minimal missing-implementation create/FIFO
  fixture. This is not a claim of requirement-by-requirement Queue TDD.
- `queue-{lock,boundary}-{red,green}.txt`: separate behavioural model regressions
  for native mutex timeout ownership and exact maximum absolute tick rejection.
  `queue-actual-native.txt` additionally exercises the actual pinned kernel.
- `monotonic-*-{red,green}.txt`: separate common/provider behavioural cycles.
- `sysroot-{red,green}.txt`: narrow compile-option classification regression.
- `execution-regression.txt`, `networking-regression.txt`: existing independent
  consumer/native/cross-build preservation. `evkb-cross-build.txt` is compile-only.
- `resource-*`: same no-op fixture with six APIs retained, static size accounting,
  source/ELF/configuration identities and reproducible report commands. The
  tracked measurement script and CI upload retain complete reports separately.
- `implementation-reviews.md`: independent review findings, resolutions and scope.
  `design-reviews.md` is historical, with subsequent human approval provenance.

Reproduce using commands in `../../messaging-validation.md`. Failure/setup logs
are classified in that document; native wrapper setup failure is not runtime RED.
No hardware operations or physical timing/stack measurements were performed.

Final local results: messaging canonical command exit0 (Host2/2, native11/11,
combined EVKB cross-build); build safeguards295/295; format/dependency commands
exit0; independent full configured analysis exit0. A previously started batch
would repeat that same analysis after its passing295-test run; its duplicate
analysis was intentionally terminated (batch exit143). The retained independent
`static-analysis.txt` is the complete successful analysis result.
