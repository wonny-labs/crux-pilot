# Independent review — ROM-preserving candidate

These are technical software assessments by agents who did not author the implementation. They grant no production, hardware, merge or Issue-closure authority.

| Review | Verdict and scope |
| --- | --- |
| Boot/profile slice, `c1d788d` | Spec PASS / quality PASS: exact FCB byte comparison, consumed read-confirm predicate, actual clock getter guards, staged inputs and retained tests |
| Command slice, `7ef53d9` | Spec PASS / quality PASS: actual native helper mappings, initial/post-command readiness, RAM error/restore semantics, bounded requests, metadata and controlled tests |

The command Reviewer independently matched all 21 source and 14 artifact hashes. The reviewed command-slice ELF is `fc6527623541621f6615d445a3c82a4843b9b7bd780c68ddbc3a6d6a51fdfe03`; BIN `41b7b7ce6ed878c841978aefedb7a1ba5ae644d88c1cfb79bf772682d6a91bdf`.

The automated B/BL walk is not a general ARM proof. Supplemental inspection of the selected 18 bodies counted 47 BL, 21 unconditional B, 56 conditional B, 10 CBZ and 4 CBNZ. Every conditional/CB edge remained internal to its containing function; there was no table or indirect operation dispatch. Special PC-writing epilogues were matched to caller-save prologues. All actual SP changes agreed with the reported 308-byte conservative sum of current reachable frames, fitting the 1024-byte operation stack; exception stack remains separate. Recheck these omitted branch/alias/SP/data forms after relevant image changes. A future automated PASS alone is insufficient.

Successful transfer plus WIP-clear establishes software completion sequencing, not that a protected or misidentified NOR accepted an intended mutation. Identity/protection and fitted-part status semantics, real elapsed deadlines, physical execution of relocation verification and the mutable-data admission policy, cache/XIP/exception behaviour, initial watchdog/supervisor/other-master state, target File/Configuration binding and physical persistence remain unqualified. Ordinary target submission and native media entry remain refused. **Physical status: NOT READY. M4 incomplete.**

At `cc02aab`, the separate whole-change Challenger returned **PASS for bounded Draft publication**, with no new blocking finding. It independently matched all 21 final source-input hashes, the 12 historical boot/profile hashes and final ELF/BIN identities, and inspected machine-code submission, elapsed-refusal and context guards. It found the selected native helper/dispatcher experiment proportionate and useful while retaining every unfinished target safety/binding gate above. It did not rerun unchanged tests or grant merge/production/hardware authority.

## Relocation follow-up

Independent spec and quality review of source `133ba3b`: **PASS**. The Reviewer matched all 25 input and 24 artifact hashes, verified startup/init-array order, bounded descriptors before access, phase-correct mutable handling and latched failures, and read the 55-check results. The final ELF is `e37efab94e11dbe20b045f432f0b6858cfe78e496cb33597a2b1d64f41c1e3b3`, BIN `062c9c8aab11dd8967d52335ab3787e244570f0cd2a41295e60bfede4f08c636`. All 18 busy functions and six consumed-data layouts retain the previous verified identity, permitting reuse of the earlier manual branch/prologue audit.

This closes the identified software verification gap without recopying mutable state. It does not establish continuous mutable integrity, actual hardware execution, cache/master/watchdog/clock state, target admission, production adoption or physical persistence. Target submission remains refused.

The separate follow-up Challenger returned **PASS for Task 7 Draft publication**, no new material findings. It matched the five owned, thirteen retained and seven startup inputs, exact final ELF/BIN, and actual init/preflight/generated-relocation behavior. Existing busy-code byte identity allows the prior manual audit to be reused. Neither review grants timer/LUT adoption, target admission or hardware authority.

## GPT2 and identity follow-up

Independent spec/quality review: **PASS** for `b2d21c1` (GPT2/lower waits) and **PASS** for `96a70ac` (identity/restore/WEL). No material implementation finding remained. The Reviewer matched95 and107 respective manifest entries, the final64/73-method evidence, actual lifecycle/error paths and renewed22/28-body machine-code audits. Actual stack instructions support380/484-byte conservative sums within1024bytes. Target submission and outer elapsed qualification remain constant refusals; no timer initialization or identity startup caller was added.

The full-table reservation is a software composition proof, not actual hardware ownership. LOCKED=1/readback semantics, fitted part/revision/grade, status/protection/write acceptance, real deadlines and external state remain unqualified. The independent review identifies the existing simulator-only provider guards, native_sim ABI flags, missing public read-result path and intentionally failed device initialization as remaining target integration work requiring its separate scope decision. These reviews grant no production, hardware, merge or closure authority.
