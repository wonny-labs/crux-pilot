# System software evidence

Captured during implementation of source commit `042086b`, based on `bf048c1`.
Paths and trailing whitespace are normalized. Source/config input hashes, native
module identities and ELF/config/devicetree hashes preserve the selected build
identity; the original generated artifacts are retained locally and the final
hosted workflows publish their own exact-HEAD reports.

- Common/Host missing-capability RED, absent-service clear RED and green Host/
  sanitizer results are separate from consumer policy/provider checks.
- `sample-policy-red.txt` demonstrates the rejected1000/1000ms sample freshness;
  corrected consumer evidence covers approved500/16000ms boundaries.
- `composition.txt` intentionally retains the initial failed11-case run: disabled
  watchdog failed before the intended diagnostic because its HAL header was no
  longer selected. `composition-disabled-green.txt` and `evkb_disabled.txt`
  record the corrected case; the other10 cases passed in the original run.
- `consumer-configure-failure.txt` is a development setup failure, not feature RED.
- `twister.txt` covers two executed configurations and three cases including real
  native HWINFO and the cross-task nonreturn reset hook.
- `evkb-binding-final.txt`, `size.txt`, `selected.config`, `modules.txt`,
  `sha256.txt` and `input-sha256.txt` describe the final all-six-operation image.
  Flash40,848B / RAM7,616B are whole-image link totals, not physical measurements.

See [validation and limitations](../../system-validation.md). Final acceptance
also requires four hosted results on the published PR HEAD. Nothing here
approves hardware operations, merge or Issue closure; old process-exit delays
remain unresolved.
