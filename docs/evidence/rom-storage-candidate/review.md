# Independent review — ROM-preserving candidate

These are technical software assessments by agents who did not author the implementation. They grant no production, hardware, merge or Issue-closure authority.

| Review | Verdict and scope |
| --- | --- |
| Boot/profile slice, `c1d788d` | Spec PASS / quality PASS: exact FCB byte comparison, consumed read-confirm predicate, actual clock getter guards, staged inputs and retained tests |
| Command slice, `7ef53d9` | Spec PASS / quality PASS: actual native helper mappings, initial/post-command readiness, RAM error/restore semantics, bounded requests, metadata and controlled tests |

The command Reviewer independently matched all 21 source and 14 artifact hashes. The reviewed final ELF is `fc6527623541621f6615d445a3c82a4843b9b7bd780c68ddbc3a6d6a51fdfe03`; BIN `41b7b7ce6ed878c841978aefedb7a1ba5ae644d88c1cfb79bf772682d6a91bdf`.

The automated B/BL walk is not a general ARM proof. Supplemental inspection of the selected 18 bodies counted 47 BL, 21 unconditional B, 56 conditional B, 10 CBZ and 4 CBNZ. Every conditional/CB edge remained internal to its containing function; there was no table or indirect operation dispatch. Special PC-writing epilogues were matched to caller-save prologues. All actual SP changes agreed with the reported 308-byte conservative sum of current reachable frames, fitting the 1024-byte operation stack; exception stack remains separate. Recheck these omitted branch/alias/SP/data forms after relevant image changes. A future automated PASS alone is insufficient.

Successful transfer plus WIP-clear establishes software completion sequencing, not that a protected or misidentified NOR accepted an intended mutation. Identity/protection and fitted-part status semantics, real elapsed deadlines, runtime validation of all relocated regions, cache/XIP/exception behaviour, initial watchdog/supervisor/other-master state, target File/Configuration binding and physical persistence remain unqualified. Ordinary target submission and native media entry remain refused. **Physical status: NOT READY. M4 incomplete.**

The separate whole-change Challenger assessment is recorded after reviewing the combined source and final evidence.
