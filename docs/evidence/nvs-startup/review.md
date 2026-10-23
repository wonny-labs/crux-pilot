# Independent review record

Scope: initial NVS scan read-error candidate, simulator fixture, copy safety,
reproducibility and retained evidence on Crux baseline `753dee7`. Separate
Reviewer and Challenger agents inspected actual sources/configuration/evidence;
neither authored the change. These are technical assessments, not GitHub formal
approval or authority for adoption, hardware operations, merge or issue closure.

Reviewer: **PASS — bounded software-only PoC review gate**. Verified the two
negative-result guards and existing exit path, first/second fault timing, proxy
operation visibility, replacement of exactly one compiled NVS source, unchanged
pinned input and retained hashes. Independently ran four copy-safety tests.

Challenger: **PASS — bounded PoC review gate**. Independently checked the pinned
control flow, fixture assumptions, preservation claims, reproducibility and
maintenance burden. Independently ran four safety tests and matched all handler
and input hashes. No material unresolved finding remains.

Review corrections before PASS:

- Formatting moved a non-self-contained private header before type declarations;
  preserve a separate include group and rerun final original/candidate fixtures.
- Reject copy destinations inside the resolved Zephyr tree, including directory
  aliases. Use a fresh staged inode and atomic replacement so an existing
  candidate symlink/hardlink cannot overwrite an upstream file. Four fake-tree
  tests cover these paths; the new tests failed before their respective fixes.
  Final real candidate generation matches the source hash already compiled.

Accepted evidence: original two controls pass/six fault cases fail; candidate
8/8 pass; unchanged upstream healthy suites with candidate 36/36 pass; safety
checks 4/4 pass on author macOS/Linux and independently by both reviewers.

Limits: two initial comparison sites only, simulated Flash, kernel-mode private
proxy. Later startup/recovery faults, reused ready mounts, target Flash, XIP and
physical storage safety remain unqualified. No production patch/layout/backend
is adopted. #14/#15/#16 and M4 retain their existing approval/acceptance gates.
