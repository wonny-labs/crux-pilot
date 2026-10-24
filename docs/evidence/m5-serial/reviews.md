# Independent software reviews — Serial

Date: 24 October 2026. Reviewers independently read the approved contract/plan,
actual common/native source, owned tests and retained evidence. Neither authored
implementation. Assessment covers the source recorded in `source-sha256.txt`;
final committed-revision CI and physical qualification are separate gates.

## Reviewer

Initial **REVISE**: Zephyr unready device returned INVALID_STATE instead of the
approved NOT_READY; controller reused a constant request/ACK and could accept a
previous response after reopen. Corrected tests reproduced both findings before
fixing status mapping and adding caller-supplied transaction identity.

Affected **PASS**: inspected the actual status/nonce corrections and RED/GREEN
records, plus logging-owner correction found independently by Challenger.

Final implementation **PASS**: common lifetime/admission, Linux partial progress
and deadline, permanent Zephyr callback gate, bounded buffers, abortive cleanup,
fault latching, ownership and public dependency isolation are coherent. Private
helper argument reordering preserves behaviour; intentional test diagnostic
suppressions are narrow. No material source finding remains.

## Challenger

Initial **REVISE**: separately chosen `zephyr,log-uart.uarts` devices bypassed
console-only logging reservation; also independently identified the controller
stale ACK and unready status defects. Actual logging composition reproduced the
ownership failure before the binding checked all selected UARTs.

Affected and final implementation **PASS**: inspected corrected ownership,
caller nonce, status mapping, actual regression evidence, permanent callback
retirement, positive commitment and deadline propagation. No unresolved material
source/documentation finding remains. No additional architecture is needed.

## Evidence and limits

Both inspected Host Serial13/13, Host consumers5/5, Zephyr4+2+1 executed cases,
logging ownership RED/GREEN, CSI expected compile rejection, EVKB fixture build
and 632B owned static symbols, boundary affected1+16 PASS, and focused analysis.
At their final inspection, last sanitizer/EVKB rebuild completion was pending;
the author must confirm actual completion, then exact committed-HEAD hosted CI.
These review verdicts do not imply those pending gates passed.

The intermediate full Host process-exit timeouts remain unresolved. The original
full safeguard run's read-only probe errors are retained as environmental
failures, followed by a writable-copy affected recheck. No hardware, merge or
issue closure is authorised or implied.

Author completion check after review: final affected ASan/UBSan Serial13/13 and
consumer5/5 PASS; final Zephyr4+2 PASS and EVKB relink PASS (42,964B/8,064B).
See `final-sanitize.txt` and `final-zephyr.txt`. Hosted CI is still separate.
