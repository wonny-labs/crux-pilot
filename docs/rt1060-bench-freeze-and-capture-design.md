# The bench image: freezing the patched Zephyr copy (B2) and a capture window above 300 s (B4)

Status: **design for review**, in a Draft PR. Refs #183. Documents only: this change adds no code, script, test or fixture, edits no existing file, and **authorises nothing**: no implementation, no board operation, no registration of the bench profile. The bench profile stays [built, not registered](board-image-profiles.md#the-bench-profile-built-not-registered). Nothing here ran on a board.

| Part | Decision state | What this document does |
| --- | --- | --- |
| **B2**, the freeze of the patched Zephyr copy | **Decided by the owner on 2026-11-09: option 2** ([the options](rt1060-bench-image-plan.md#b-the-manifest-source-and-the-pinned-set)): stock Zephyr stays the west project, and the copy's identity is frozen as extra artefacts and checked against the `patched_zephyr` block of the bench manifest. The approval reached this repository as a quote relayed by the Crux Secure Communication session: "1-8까지 너의 추천대로 진행해줘" (in English: proceed with items 1 to 8 as you recommend; the recommendation list of the owner's question; this session has not seen the list itself). | Designs option 2 for a **list** of patches, because a second patch (`tcp_init_isn`, session C11) will join. Implementation still needs its own PR, review and the registration approval. |
| **B4**, a capture window above 300 s | **Not decided. Not approved.** | **Proposes** options with a recommendation for the owner to approve **before any implementation**. |

Evidence labels used below. **Fact**: read in source, cited as `file:line` at `origin/main` `298280fe`. **Derived**: arithmetic on facts, shown. **Recommendation**: a choice this document proposes. **Unknown**: not read, not measured, or not available.

## Result

| Question | Answer |
| --- | --- |
| B2: what is frozen, and where? | Per patch: name, SHA-256 and the copy of its bytes. Per patched file: path, SHA-256, the patches that touch it and a copy of its bytes. The marker. A tree digest of the whole copy (`crux-zephyr-tree-v1`), taken **before and after** the build. All of it in one record, `patched-zephyr.json`, plus the bench manifest itself, as two new frozen roles of the bench profile. |
| B2: what refuses? | `check_image` of the bench profile, on frozen bytes only, refuses a missing, extra, changed or reordered patch, an unpatched tree, a patched file that differs, a tree digest that differs, a manifest block of the old single-patch shape, and a copy that was not the compiled one ([table](#what-check_image-refuses)). |
| B2: does it weaken the dirty-pin refusal? | **No.** The stock `zephyr` project must still be clean and at the pin, by the same two checks as today. The copy is never accepted as that project ([interaction](#interaction-with-the-dirty-pin-refusal)). |
| B2: which owner choices are inside the approved option? | Two refinements go beyond its wording and need a yes or no: **a tree digest declared in the committed declaration**, and **a re-derivation of the expected tree from the stock checkout** ([questions](#b2-open-questions)). Recommended: yes to both. |
| B4: recommendation | **Option X**: a separate read-only mode that only reads the UART, with no probe command at all. [Comparison](#comparison-and-recommendation). The decision is the owner's. |
| B4: what the load run needs | A window of about **2,000 to 2,080 s** by the plan's own figures, proposed as **2,400 s** ([arithmetic](#what-the-load-run-needs)). |
| What was not verified | The shape of the C11 patch (its branch holds no change yet), the file list of the wolfSSL socket patch, real report rates, step durations of the guard checks, and every behaviour on a board ([list](#what-was-not-verified)). |

---

# Part 1: B2, the freeze of the patched Zephyr copy

## Facts that the design rests on

| # | Fact | Source |
| --- | --- | --- |
| F1 | The bench image is built with `ZEPHYR_BASE` pointing at a **copy** of the pinned Zephyr tree. The copy is not a west project and has no `.git`. | `scripts/prepare-wolf-tls-zephyr.sh:30` (`git archive HEAD ':!doc' ':!samples' ':!tests' \| tar -x`), `docs/rt1060-bench-image-plan.md:192` |
| F2 | The copy is made by `patch --strip=1 --no-backup-if-mismatch --quiet`, a `*.rej` check, a hash check of **one** file, then the **patch file is deleted** and the marker `.crux-wolf-tls` is written as `<pin> <patch sha256>`. | `scripts/prepare-wolf-tls-zephyr.sh:35-39` |
| F3 | The build-time guard compares the marker text, and the hash of **one** patched file. Nothing else in the copy is hashed. | `tests/zephyr/management_rt1060_compile/bench_guard.cmake:252-264` |
| F4 | The declaration holds one patch: `marker_file`, `zephyr_pin`, `patch_name`, `patch_sha256`, `patched_file`, `patched_file_sha256`. The manifest block written from it has `decision`, `pin`, `patch`, `patch_sha256`, `patched_file`, `patched_file_sha256`. | `bench_declaration.json:62-70`, `bench_guard.cmake:443-448`, `docs/evidence/rt1060-management-image-feasibility/image-plan-c8/build/bench-manifest.json` (block `patched_zephyr`) |
| F5 | The socket patch changes more than one file: it adds files with an Apache header, and it changes `subsys/random`. So a hash of one file does not cover what it did. | `patches/zephyr/README.md` (section "wolfSSL TLS socket backend"), `docs/rt1060-wolf-entropy.md:51` |
| F6 | The freeze sees only west projects. `workspace_inputs` runs `west list`, then `git rev-parse HEAD` and `git status` in each project, and **raises** unless `HEAD` equals the manifest revision and the tree is clean. `frozen_inputs` requires the pinned `zephyr` project at its path, `dirty` false, `revision == git_commit == pin`. The copy is in neither. | `scripts/record-build-provenance.py:114-133` (refusal at `:127-129`), `tests/target/prepare_m5_network.py:163-165` |
| F7 | `check_image(binary, elf, config)` has three arguments and is called twice: in `prepare` and in `verify`, which `prepare` also runs. Its result must equal `record['image']`. The roles of a freeze are a fixed set, `ROLES`, minus `errno` for a profile without errno evidence. | `prepare_m5_network.py:35`, `:194`, `:224`, `:237-238`, `:269-271` |
| F8 | `verify` also runs in the driver on the Mac **before every load** (`reverify`). The Zephyr copy lives in the build environment, not on the host that drives the board. So a check at `verify` time can read frozen artefacts and nothing else. | `tests/target/program_m5_image.py:434-439`, `docs/rt1060-bench-image-plan.md:83` (copy at `/ws/zephyr-wolf` in the container) |
| F9 | The tool set is frozen from `tests/target/*.py` and `ImageProfile.tool_scripts`, so a new module in `tests/target/` is frozen as a tool without further wiring. | `prepare_m5_network.py:168-173` |
| F10 | `ImageProfile` has a pinned field list: a test fails if a field is added, so a change is reviewed. | `tests/build/test_m5_program_capture.py` (`test_the_fields_of_a_profile_are_exactly_the_reviewed_ones`) |
| F11 | The source snapshot already has a hash framing for a tree of files (`path \0 kind \0 mode \0 sha256(content)`, paths sorted, `.DS_Store` skipped, symlinks hashed by target text). | `scripts/record-build-provenance.py:51-79` |
| F12 | The session C11 branch `rt1060-bench-c11` (under `.worktrees/`) is at `52e894e8`, equal to the `main` it was cut from, with a clean tree and no commit of its own. **The shape of the `tcp_init_isn` patch could not be read.** The need is documented: `tcp_init_isn` ignores a failed `sys_csrand_get` and keeps an all-zero key. | `git -C <worktree> status`, `docs/rt1060-wolf-entropy.md:51`, `:147` |
| F13 | There is a second, existing way to patch Zephyr without touching the tree: a build-local **replaced source** (`prepare-*.py` plus a CMake replacement, a `.json` record of original, patch and output hashes). It is not what the copy uses. | `patches/zephyr/README.md` (the MDIO, Mbed TLS entropy and MMC sections) |

## The design

### 1. The declaration and the manifest block, as lists

The block becomes a list of patches and a list of patched files, plus a declared tree digest. The shape below is a **proposal**; names that C11 fixes are marked `<C11>`.

```json
"patched_zephyr": {
  "schema": 2,
  "decision": "D8",
  "marker_file": ".crux-wolf-tls",
  "zephyr_pin": "dccb09599635bdff17633fa7e9dab014b91dce90",
  "patches": [
    {"name": "zephyr-tls-4.4.1.patch", "sha256": "3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda"},
    {"name": "<C11>", "sha256": "<C11>"}
  ],
  "patched_files": [
    {"path": "subsys/net/lib/sockets/sockets_tls.c", "change": "modified",
     "sha256": "106acdbb725fc72e4eca242125fd02283b0cd67403b205c7c231b2db6328552a", "patches": ["zephyr-tls-4.4.1.patch"]},
    {"path": "<C11>", "change": "modified", "sha256": "<C11>", "patches": ["<C11>"]}
  ],
  "tree": {"algorithm": "crux-zephyr-tree-v1", "sha256": "<derived once, see 2 and 7>", "file_count": 0}
}
```

| Rule | Why |
| --- | --- |
| `patches` is **ordered**: the order is the order of application. | Two patches can touch one file, and the second may not apply to the original. A reordering is a different tree. |
| `patched_files` lists **every** file that any patch adds, modifies or deletes, with its **final** hash after all patches, and the names of the patches that touch it. A deleted file has `"sha256": null`. | F3 and F5: one hash does not cover the patch. A file touched by both patches has one final hash and two names. |
| The declaration (source) holds the list and the tree digest. `bench_guard.cmake` reads the list with a loop, writes the same list into the manifest block, and keeps its configure-time check: the marker and each patched file's hash. The manifest block mirrors the declaration one to one, with the existing key name `pin` for `zephyr_pin`. | F3, F4. The build-time guard stays as the first fence; the freeze is the second. |
| A block without `"schema": 2`, or with any of the old single keys (`patch`, `patch_sha256`, `patched_file`, `patched_file_sha256`), is **refused** by `check_image`, not read as a one-element list. | The recorded manifests under `docs/evidence/` have the old shape and stay as they are (frozen evidence). No approval binds a manifest of the old shape: the profile is not registered. |
| A patch that renames a file, changes a mode, or carries a binary hunk is refused by the freeze. Only text additions, modifications and deletions are understood. | The touched-file set is parsed from the patch bytes (the `diff --git a/x b/x` headers); a parser for more is more risk than the need. **Unknown** whether the wolfSSL patch is purely of that kind: if it is not, the freeze refuses loudly and this rule is revisited. |

### 2. The marker for N patches

The marker keeps today's first field and today's one-patch text, and extends it: `<pin> <sha256 of patch 1> <sha256 of patch 2>` followed by a newline, hashes in application order. For one patch the text is byte-for-byte what `scripts/prepare-wolf-tls-zephyr.sh:39` writes now. The guard and the freeze both build the expected text from the ordered list, so a copy made with only the first patch (an old marker) is **missing** the second, and a copy made in the other order is **reordered**.

The script that writes the marker is on a protected path. Either the C11 work changes `scripts/prepare-wolf-tls-zephyr.sh` within an explicit human approval that covers that change, or it adds a **new** sibling script (a new file is allowed within a task) that applies the second patch and extends the marker. The design needs only the marker contract above, not the choice.

### 3. The tree digest, `crux-zephyr-tree-v1`

A new module in `tests/target/` (F9 freezes it as a tool). It follows the framing of F11 and differs where a copy of a git archive needs it to.

| Question | Rule | Reason |
| --- | --- | --- |
| Which entries | Every regular file and every symlink under the copy root, found by walking the directory **without following symlinks**. Directories are not entries. **Empty directories are ignored.** | A `git archive` has no empty directories, and a patch may leave one behind. |
| Order and names | POSIX relative paths, UTF-8, **sorted by their bytes**, never by locale. A name that is not valid UTF-8 is refused. Two names equal under case folding are refused. | Determinism on any host. A case-insensitive file system (a macOS build) would otherwise merge two files silently. |
| Framing | Per entry: `path \0 kind \0 flag \0 sha256(content)` as raw digest bytes, with `kind` `file` or `symlink`; the digest of all entries is SHA-256. | The same unambiguous framing as F11 (the digest is fixed length). |
| Mode | Only the **executable bit**, as `exec` or `plain`. No other mode bit, no owner, no time. | `tar -x` applies the extracting user's umask, so full modes would differ between environments. The cost is stated in [the list of non-proofs](#what-this-does-not-prove). |
| Line endings | **None normalised.** The bytes are hashed as found. CRLF and LF give different digests. | A normalisation would hide a real difference. A test pins it. |
| Symlinks | The digest of the link's **target text** (`readlink`), never the target's content. A link whose target resolves outside the root, or is absolute, is refused. | The tree must be self-contained evidence. |
| Other file types | Sockets, FIFOs and devices are refused. A `.git` entry anywhere under the root is refused. | The copy is not a repository (F1); a pointed-at checkout is a mistake. |
| Excluded | Exactly: `.DS_Store`, any `__pycache__` directory, and `*.pyc` and `*.pyo` files. The list is written into the record and into the digest's algorithm name, so a change is a new algorithm. | The build runs Zephyr's Python scripts, which can write bytecode into `ZEPHYR_BASE`. Without the exclusion the post-build digest would differ from the pre-build one for no real reason. |
| The marker | **Included** in the digest like any other file. | It is part of what was prepared. |
| What is hashed | The **copy**, not the stock tree. | The stock tree is checked by the existing freeze (F6). |

**Unknown:** whether `git archive` converts line endings or expands attributes on the machine that builds. The digest is therefore always derived **on the build environment** and the declared value is confirmed by a second, independent derivation before it is committed ([order of work](#order-of-work)).

### 4. What the freeze records, and where

A new step, in two calls around the build, in `tests/target/` (a module plus a thin command line), orchestrated by a **new** script under `scripts/` that is added to the profile's `tool_scripts`, in the pattern of `scripts/test-networking-physical.sh --freeze` for M5. The new script is a new file on a protected path: it is listed in the PR description and gets the material independent review.

| Step | Command (names are proposals) | What it does |
| --- | --- | --- |
| 1 | `record-build-provenance.py --capture-inputs` (unchanged) | Captures source and west inputs. |
| 2 | `patched_zephyr.py capture --copy DIR --out FILE` | Before the build: the marker text and the tree digest of the copy. Refuses a copy that fails any rule of the digest. |
| 3 | the build and `rom_report ram_report` (unchanged) | |
| 4 | `patched_zephyr.py freeze --copy DIR --zephyr DIR --patches DIR --manifest BENCH_MANIFEST --compile-commands FILE --pre FILE --out DIR` | After the build, all of: **(a)** the tree digest again, equal to the pre-build one; **(b)** the touched-file set parsed from each patch equals the declared `patched_files` and the set of files that differ from the stock-derived tree; **(c)** the marker equals the text built from the declared list; **(d)** each patch's bytes (read from `--patches`, by name) hash to the declared value; **(e)** the re-derivation: the stock tree from `git archive <pin> ':!doc' ':!samples' ':!tests'` of the clean west `zephyr` checkout, the patches applied in order with the **same `patch` command line** as F2 into a temporary directory, its digest equal to the copy's and to the declared digest, and different from the digest of the stock tree alone; **(f)** the compile-commands check ([below](#the-compiled-tree-is-the-copy)); then it copies the patch files, the patched files, the marker and a per-file listing `tree-files.tsv` into `<build>/patched-zephyr/` and writes `patched-zephyr.json`. |
| 5 | `record-build-provenance.py --build-dir ... --inputs ...` (unchanged) | |
| 6 | `prepare_m5_network.py prepare --profile ...`, then `verify --sha256 ...` | The two new roles are frozen and re-hashed. |

`patched-zephyr.json` (schema 1) holds: the algorithm and the exclusion list; the marker text; the pre-build and post-build digests, file count and symlink count; the stock tree's digest and the re-derived digest; the ordered patches with their hash, size and the frozen copy's relative path; the patched files with `change`, final hash, `stock_sha256` (`null` for an added file), the patches that touch them and the frozen copy's path; the compile-commands counts; and the SHA-256 of the canonical JSON of the manifest's block, using the same canonical form as `firmware_digest` (`sort_keys`, compact separators). **The build directory is inside the repository**, as the recorder requires (`record-build-provenance.py:333-334`), so the folder is covered by the existing freeze pattern.

**Roles.** The bench profile gets two extra roles, `bench_manifest` (the build's `bench-manifest.json`, frozen like `.config`) and `patched_zephyr` (the record), through **one new `ImageProfile` field** (default none) next to the existing profile-dependent `errno` role (`prepare_m5_network.py:237`). Every file that the record names is also listed in `artifacts` and re-hashed by the generic loop (`:240-246`). The M5 and S2 profiles gain no role, and their tests must pass unchanged. The pinned field list of F10 changes in the same reviewed change, on purpose.

**`check_image` signature.** For a profile with extra roles the call passes them as **required keyword arguments** (`bench_manifest`, `patched_zephyr`). There is no default that skips the group: a missing artefact must be a refusal, never a pass. The three-argument calls of the existing bench tests are updated with fixtures, which changes no assertion's meaning.

### The compiled tree is the copy

`compile_commands.json` is already a frozen role. The freeze requires that every translation unit of a Zephyr source path in it lies **under the copy's path as the build saw it**, that **none** lies under the stock project's path, and that at least one does. This shows **which directory the build compiled from**, not what was in it (the digests do that). The paths are compared as strings; a freeze run from a different mount point is refused, not translated. **Unknown:** how the paths look in the real container build (a symlinked `/ws`, for example); the first real freeze is the first contact.

### What `check_image` refuses

`check_image` reads the BIN, the ELF, the `.config` and the two frozen roles. It never reads the copy (F8). Each row is one test.

| Condition | Refusal |
| --- | --- |
| The manifest has no `patched_zephyr` block, a block without `"schema": 2`, or any old single key | "legacy or missing patched Zephyr block" |
| The record is missing, has an unknown schema or algorithm, or its manifest-block hash differs from the frozen manifest's block | "record and manifest differ" |
| A declared patch is **missing** from the record, from the marker, or from the frozen artefacts | "missing patch NAME" |
| The record or the marker lists a patch the block does not declare (**extra**) | "unexpected patch NAME" |
| A patch's frozen bytes do not hash to the block's value (**changed**) | "changed patch NAME" |
| The order of the record or of the marker's hashes differs from the block's (**reordered**) | "patches out of order" |
| The marker is absent, or is not `<pin> <hashes in block order>` | "marker differs" |
| The record's pin differs from the block's or from `bench_image.PINNED['zephyr']` | "pin differs" |
| **Unpatched tree**: a patched file is missing from the record, equals its `stock_sha256`, or the record's re-derived stock digest equals the copy's digest | "tree is not patched" |
| A patched file's final hash differs from the block's, or the record has a patched file the block lacks, or the block has one the record lacks | "patched file differs" |
| **Hash mismatch**: the record's tree digest, file count or algorithm differs from the block's; the pre-build and post-build digests differ; the re-derived digest differs or is absent | "tree digest differs" |
| `sources_under_stock` is not 0, or `sources_under_copy` is 0 | "build was not from the copy" |
| A frozen artefact the record names is missing or its bytes do not hash to the record's value | "frozen artefact changed" |

The facts that `check_image` returns include the patch names, hashes and the tree digest, so the existing equality with `record['image']` (F7) also binds them into the approved manifest digest.

### Interaction with the dirty-pin refusal

| Case | Result |
| --- | --- |
| The stock `zephyr` west project is dirty or off the pin (someone patched the west checkout in place) | Refused **as today**: `workspace_inputs` raises (F6, `:127-129`) and `frozen_inputs` requires `dirty` false and `revision == git_commit == pin`. A valid patched-Zephyr record does not change that. A test pins it for the bench profile. |
| The copy is registered as the `zephyr` project, or its path equals, contains or sits inside a west project path | Refused by the new step before anything is recorded. Today it would refuse only by accident (a `git rev-parse` failure or, worse, a parent repository's `HEAD`). |
| The copy holds a `.git` | Refused by the digest (rule above). |
| The pin differs between the west checkout, the marker and the block | Refused: three places, one value. |

Option 2 adds evidence next to the existing refusal. It adds **no exception** to it.

### Tests first

Following test-driven development, in this order, each red before its code. Fixtures are small synthetic trees built inside the test (a few files, an executable file, a symlink, an empty directory, a `.DS_Store`, a `__pycache__`), and the real block, pin and hashes of F4 as a committed extract.

| # | Tests (all host-only, no Docker, no board) |
| --- | --- |
| T1 | **Known answer** for `crux-zephyr-tree-v1`: the digest of one tiny tree is a literal in the test, and a second, deliberately different implementation in the test file (another walk order) agrees. |
| T2 | **Determinism**: creation order, readdir order, mtimes, umask-dependent mode bits and owner do not change the digest; only the executable bit does. |
| T3 | **Sensitivity and refusals**: one byte; CRLF against LF; a rename; a changed symlink target; an extra or missing file; two names equal under case folding; a symlink to the outside; a FIFO; a non-UTF-8 name; a `.git`; empty directories and the excluded names ignored. |
| T4 | **Marker**: for one patch the text equals today's `<pin> <sha256>` byte for byte; for two it is the ordered list; the guard's expected text and the freeze's agree. |
| T5 | **Patch parsing**: the touched-file set of a synthetic unified diff (added, modified, deleted); a rename, a mode change and a binary hunk are refused. |
| T6 | **Freeze record**: built from the fixtures it is deterministic (sorted keys), pre and post digests are both recorded, and a file changed between them is refused. |
| T7 | **`check_image` accepts** the one-patch case (today's block migrated to the list) and the two-patch case, and **refuses each row** of the table above, including a file touched by both patches and the added file. |
| T8 | **Re-derivation** with the real `patch` command on a synthetic stock tree and patches. It is skipped, and reported as skipped, where `patch` is absent: a skip is not a pass. |
| T9 | **Compile commands**: all under the copy passes; one under the stock path, or none under the copy, is refused. |
| T10 | **Wiring**: the bench profile freezes with the two extra roles and refuses a missing or altered record at `prepare` and at `verify`; the M5 and S2 profiles and all their existing tests pass unchanged; the field-list pin is updated deliberately. The dirty-pin cases of the table above. |
| T11 | **Declaration and guard**: `bench_declaration.json`, `bench_guard.cmake` and `check_bench_guard.py` for lists, with the existing `ZEPHYR_BASE_BROKEN` cases (`check_bench_guard.py:319-322`, `:496-506`) extended by a missing, an extra and a reordered patch. `test_management_bench_declaration.py` is updated for the list (its current assertions name the single patch). |

### Order of work

1. The list schema in the declaration and the guard, with T4 and T11, together with the C11 patch (its hash, files and marker).
2. The digest module and the freeze step (T1 to T6, T8, T9).
3. The profile roles and `check_image` (T7, T10).
4. The first real freeze on a real build: **declare the tree digest only after two independent derivations agree** (one in the build environment by the script, one by hand), and record both.
5. Registration of the bench profile is **separate**, after B4 and the other blockers of [the plan](rt1060-bench-image-plan.md#what-blocks-the-plan-today), and needs its own approval.

Review: the independent `reviewer` agent (Opus), supplied with the base and head SHAs, the diff command, the diff as a file, the changed protected paths, these acceptance criteria and the checks as run by the author. The PR lists each changed protected file from `git diff --name-only --no-renames origin/main...HEAD` with the approval for it. Existing assertions that must change (the single-patch ones) are quoted in the PR, because a test may be changed only when the approved behaviour changes.

### What this does not prove

| Not proved | Why |
| --- | --- |
| That the **patches are correct or safe**, or that the `tcp_init_isn` change fixes anything | The freeze identifies bytes. Content is a review. Behaviour needs tests and, for the target, a board. |
| That the wolfSSL patch is the one its author meant, or that it is licensed for use | Only its hash is pinned. The licence status is open ([README](../patches/zephyr/README.md)). |
| That a patch generated for Zephyr 4.4.1 gives a correct tree on this pin | The re-derivation proves the copy equals "pin plus these patches applied by this `patch` command", not that the result is right. |
| That the copy equals "pin plus patches" **without** the re-derivation, or on a host whose `patch` or `git archive` behaves differently | Without it the declared digest comes from the copy itself and proves only that the copy did not change after it was declared. With it, the proof holds for the tools that ran. |
| That the ELF was compiled from these bytes | The digests before and after bracket the build, and `compile_commands.json` shows the directory. A file changed and restored inside the window, or a compile from a different tree with the same paths, is not seen. There is no reproducible-build proof here; the two-build comparison of the plan is separate. |
| Mode bits other than the executable bit, owners, times, empty directories | Left out of the digest on purpose. |
| The content of anything outside the copy: other modules, the toolchain, **build-local replaced sources** (F13) | Their freezes are other records. |
| The stock Zephyr checkout beyond its pin and a clean status | Git's own integrity is assumed. |
| Anything about hardware | Nothing here ran on a board. |

### B2 open questions

| # | Question | Recommendation |
| --- | --- | --- |
| Q1 | Declare the tree digest in the committed declaration, so a change of the pin or of a patch forces a reviewed edit? Without it the digest is only recorded, never compared with anything independent. | **Yes.** |
| Q2 | Require the re-derivation (item e of step 4)? It needs the patch files at freeze time, the `patch` tool and the stock checkout. | **Yes.** It is the only step that ties the copy to "pin plus named patches". Fall back to Q1 alone only if the owner rules `patch` out. |
| Q3 | How does C11 apply its patch: **into the copy** (this design) or as a **build-local replaced source** (F13)? The shape could not be read. A replaced source is outside the tree digest and needs a `replaced_sources` list with the same refusal rules, which this document does not design. | Into the copy, so one record covers both patches. Ask C11 before it writes the patch. |
| Q4 | Marker: edit the protected `scripts/prepare-wolf-tls-zephyr.sh`, or add a sibling script? | A sibling script, to keep the protected file unchanged. The approval must cover whichever is chosen. |

---

# Part 2: B4, a capture window above 300 s

## Facts that the design rests on

| # | Fact | Source |
| --- | --- | --- |
| G1 | The window of a profile is a whole number of seconds from 1 to `MAX_CAPTURE_SECONDS` = 300, checked in `ImageProfile` and again in `CaptureSpec`. The bench profile sets it to the maximum. | `tests/target/image_profile.py:14`, `:54-55`; `tests/target/uart_raw_capture.py:254-256`; `tests/target/bench_image.py:27` |
| G2 | The series budget is `series_seconds=900`, a default of `Driver.__init__`. `main` does not pass it, so there is no option for it. The clock starts when the driver is constructed. | `tests/target/program_m5_image.py:80-81`, `:89`, `:442-443` |
| G3 | Every probe step requires `now + 120 < end` before its checks and `now + 45 < end` after them. The capture additionally requires `now + 30 + window < end` before the load. | `program_m5_image.py:44`, `:133`, `:136`, `:187` |
| G4 | The window runs **after** the one load command has returned. `window()` is the last call of `program_captured`, and nothing in it starts a probe command. | `program_m5_image.py:176-212` (`:210`) |
| G5 | `preconditions` runs the guard check with **90 s** of validity required (`now + 90 < session['deadline']`), the ownership inventory and the adapter check. It runs before each probe command and **never while a capture is open**. | `program_m5_image.py:337-354` (`:338`), `tests/target/capture_m3_network.py:81-112` (`:96`) |
| G6 | `acquire` takes the board lock with a **non-blocking** `flock` and the UART with `exclusive=True` and `TIOCEXCL`, and the driver holds both until `__exit__`. A second driver for the board cannot start while one runs. | `program_m5_image.py:375-380`, `:98-110` |
| G7 | One use of each mode per evidence parent: `consume` writes `m5-driver-<mode>.consumed` with `open('x')`. | `program_m5_image.py:291-299` |
| G8 | Modes are `identify`, `program`, `restore`; a capture is allowed only for `program`, and a test pins that. | `program_m5_image.py:42`, `:83`; `tests/build/test_m5_program_capture.py` (`test_only_program_mode_may_carry_a_capture`) |
| G9 | The capture keeps every chunk in memory (`self.raw`) and writes `uart-rx.bin`, `receive.jsonl` (hex) and `poll.jsonl` (one line per read, which returns after the port's 0.02 s timeout). At close it copies the bytes once more, builds `lines.json` and walks every byte in Python for the non-ASCII count. | `uart_raw_capture.py:153-175`, `:209-239`, `:64-77`; `program_m5_image.py:379` (`timeout=.02`) |
| G10 | The judge needs one stream: it refuses a lost report (`seq` not the next, except `seq=1` for a boot) and a capture with no boot report. The idle report prints **once** until traffic returns. | `tests/zephyr/management_rt1060_compile/check_bench_uart.py:135`, `:263`; `report/bench_report_policy.c:42`; `report/bench_report_host_test.c:290-293` |
| G11 | The plan's own figure for the capture is "about 2,000 s": boot to `BENCH ready` up to 120 s, the clients 20 to 30 minutes, the reset and second boot 120 s, at least 40 s after the last client. | `docs/rt1060-bench-image-plan.md:229` |
| G12 | The guard of the plan is started with `--lifetime 14400`. The reset has **no mode** (B5): the owner may press the board's reset button, which an approval must name. | `docs/rt1060-bench-image-plan.md:284`, `:47` |

## What the load run needs

| Item | Value | Basis |
| --- | --- | --- |
| Capture time the plan needs | 120 + 1,800 + 120 + 40 = **2,080 s** with the longest client figure; "about 2,000 s" | Derived from G11 |
| Of which after the first boot (what a second window must cover) | 1,800 + 120 + 40 = **1,960 s** | Derived |
| Proposed window | **2,400 s** (about 15 percent above 2,080 s) | Recommendation, not a measurement |
| Worst-case bytes at line rate, 115,200 baud 8N1 = 11,520 B/s | 330 s: 3,801,600 B (the "about 3.8 MB" of `image_profile.py:14`). 2,430 s: **27,993,600 B**. A cap of 3,600 s plus 30 s: 41,817,600 B. | Derived. The real rate is **unknown** and far lower if the image prints only reports (at most one per 5 s, `docs/rt1060-bench-image-plan.md:228`); the bound is only for memory and disk sizing. |
| Memory at close | The raw bytes twice, plus `lines.json` rows, plus `receive.jsonl` at twice the raw size in hex | G9. Fine at the real rate, to be sized for the worst case above. |
| `poll.jsonl` | At most 50 lines per second (0.02 s timeout), so at most about 120,000 lines for 2,400 s | Derived from G9 |
| Guard validity the window needs | The guard must outlive the window. With `--lifetime 14400` that holds unless the guard has run longer than about 14,400 - 2,490 = 11,910 s at the check | Derived. **Unknown:** whether the deadline counts from start or from `READY`. |

## The options

### Option X: a separate read-only capture mode

A fourth mode, **`watch`** (name open), that only reads the UART and **runs no probe command**: no identity-only run, no verify, no load, no reset. It runs after `program` has finished and released the port.

| Item | Content |
| --- | --- |
| Gates that **stay**, using the same code | `validate_approval`; `consume` (one use per evidence parent, G7); `acquire` (board lock, exclusive UART, `select_uart` by the UART's USB serial, G6); `preconditions` once before the capture opens (guard, ownership inventory, adapter identity); `reverify` of the frozen manifests; the series budget; `HardwareStop` on any failure; `exist_ok=False` evidence directory; **no retry, no loop**. |
| What gets **stronger** | The long window sits in a mode that has no probe command at all. G4's property (no probe command in the window) holds by construction instead of by the order of calls in one method. There is no SWD traffic, so the target cannot be left halted by a probe step. |
| What **weakens or goes** | (1) **No probe identity.** The J-Link identity (core, debug port, probe serial, VTref) is not read, so the tool cannot tell that the board behind the UART is the board behind the probe. The UART's USB serial and the adapter identity stay bound. Offline, the capture's own `BENCH image=` line and the judge identify the image. (2) The **unattended window grows** from at most 330 s to the new cap. (3) **A new code path**, so a new review surface. (4) **A seam**: the window of `program` closes, the port is released, and `watch` opens later; bytes printed in between are lost. |
| Why no identity-only probe | **Unknown** whether the identity-only run (`q`) leaves a core running; the repository records only that `identify` with `IsHalted` "may leave the core halted". A halted image prints nothing and would silently end the evidence. The recommendation is none. |
| The seam, handled | G10: the judge needs one stream. With the 300 s window of `program` the boot report and the one idle report fall inside the first file. If the host starts **no TCP client before `watch/operations.json` shows `capture-open`**, nothing prints in the gap (the idle report is not repeated) and the two raw files concatenated give one continuous stream for the judge. **Unknown:** whether anything else prints in the gap; a report cut by the close would show as torn, and a report printed in the gap as a lost one. Either is a recorded finding, not a retry. |
| The reset (B5) | Compatible with the owner pressing the board's reset button inside the `watch` window. A driver-controlled reset during a window cannot be a separate mode: the lock and the port are held (G6). |
| Exact code points | `program_m5_image.py:42` (`MODES`), `:80-91` (`Driver.__init__`: the capture allowed for `watch` as well, `:83`), `:129-147` (`run`: the gate sequence **factored out** into one private method that `run` and the new `watch` both call, so the two cannot drift; the existing tests of `run` are the proof that nothing changed), a new `Driver.watch`, `:337-354` and `:338` (the guard check gets a `remaining` argument, default 90, and `watch` passes 90 + window), `:413-417` (`capture_spec` for `watch`), `:445-453` (`main`). `tests/target/image_profile.py`: a new field (`watch_seconds`, 0 for none) with its own cap `MAX_WATCH_SECONDS` and validation next to `:54-55`; `uart_raw_capture.py:254-256`: `CaptureSpec` takes its bound as an argument. `bench_image.py`: the new field set, `capture_seconds` **stays 300**. |
| What `program` keeps | Its 300 s cap, its 900 s budget, its 90 s guard rule and **every existing assertion** of `test_m5_program_capture.py`, `test_m5_program.py` and `test_m5_raw_capture.py`. Only `run` is refactored (no behaviour change), and those unchanged tests are the regression proof for the one mode that writes flash. The fakes of `preconditions` in those tests gain the optional `remaining` argument, which changes a fixture, not an assertion. The alternative to the refactor is to copy the gate sequence into `watch`, which lets the two drift: not recommended. |
| The load run in total | `program` (window 300 s, unchanged) then `watch` with a window of **2,400 s**, series `300 + 2,400 s` (proposed: one check needs far less than the 900 s of three Commander steps), guard validity at its one check of at least **90 + 2,400 = 2,490 s**. Two evidence directories, two guard checks, one more consumed marker. The approval text names the profile, both windows and the mode, because the plan schema has no field for them (`docs/board-image-profiles.md`, "Open for the owner"). |
| Proposed cap | `MAX_WATCH_SECONDS` = 3,600 and a window of 2,400 s (a recommendation, not a measurement). |
| Tests, first | A fake port and fake clock as in `test_m5_program_capture.py`: no `probe_command` call in `watch` in any path; refusal before opening for each gate (guard, ownership, adapter, `reverify`, series, guard validity shorter than the window); the window bounds; one use per evidence parent; a failure of the reader stops the mode with no restart; the seam case with two fakes and the judge's `seq` rule on the concatenation; the profile field pin and the `CaptureSpec` bound; `program` unchanged (the existing tests, plus a test that `capture_seconds` of the bench profile is still 300). |
| Review | [Below](#review-requirements-for-b4). |

### Option Y: raise the limits of the existing window

The minimal change. Y1 changes `MAX_CAPTURE_SECONDS` and the series default. **Y2 (recommended form of Y)** adds the window to the series only when a capture is present, so `identify` and `restore` keep 900 s.

| Item | Content |
| --- | --- |
| What **stays** | The single mode and the single evidence stream: no seam. G4's order of calls. The identity-only and entry-image steps. Every other gate. |
| What **weakens** | (1) `MAX_CAPTURE_SECONDS` is a **standing relaxation for every registered profile**, limited only by the review of that profile. (2) The series budget grows: Y1 for all modes, Y2 for `program` with a capture. (3) The unattended window grows to 2,430 s after the last check, in the mode that **writes flash**. The guard has only to be valid for 90 s at that check (G5): today the window can already outlive it by up to 240 s, and under Y by up to 2,340 s unless the rule changes. (4) The mode that writes flash is itself edited. |
| Exact code points | `image_profile.py:14` (300 to 2,400, the smallest value that admits the load profile) and its comment; `program_m5_image.py:89` (Y2: `end = clock() + series_seconds + capture.seconds`, no new parameter), `:187` unchanged; the guard rule at `:338`, which needs a per-step `remaining` (the load step passes 90 + 30 + window) or the session's deadline compared in `capture_spec`'s caller, with the clock identity of the guard and the driver checked; `bench_image.py:27` (2,400). |
| Existing assertions that change | `test_bench_image_profile.py:259` (`MAX_CAPTURE_SECONDS == 300`), `:262` (a window of 2,000 refused), `:265-267` (300 and 900); `test_m5_program_capture.py:211-220` (a window of 300 refused at a series of 320, which Y2 would accept). AGENTS.md allows a change of an assertion only when the approved behaviour changes, so each is quoted in the PR with the approval. |
| The load run in total | One window of **2,400 s** inside `program`; series (Y2) **900 + 2,400 = 3,300 s**; guard validity at the load step of at least 90 + 30 + 2,400 = **2,520 s**; one evidence directory; the plan's steps keep their shape. |
| Tests, first | The capture-budget cases at the new numbers; the guard-covers-the-window refusal; `identify` and `restore` keep 900 s (Y2); the bench profile accepts 2,400 and refuses 2,401; every existing gate test unchanged. |

### Option Z: one gated session with a driver-controlled reset in the window

Merges B4 and B5: after the window opens the driver runs **one more** gated probe command (`r`, `g`, `q`) when the host asks.

| Item | Content |
| --- | --- |
| Why it was considered | G6: a reset **mode** cannot run while any capture holds the lock and the port, so a tool reset during the window must be inside the same driver. |
| What it breaks | G4's property: a probe command **after** the window opened. It needs its own identity, guard and ownership checks while a reader runs, a second Commander run and operation in the approved plan, and a trigger that the host controls. It is the largest change to the reviewed write path and the largest review. |
| Verdict | **Not recommended now.** It is the option to revisit only if the owner rejects the button press for B5. |

## Comparison and recommendation

| Property | Today (300 s) | X | Y (Y2) | Z |
| --- | --- | --- | --- | --- |
| No probe command while the window is open | by call order | **by construction** | by call order | **no** |
| The mode that writes flash is edited | n/a | **no behaviour change** (`run` is refactored, its tests unchanged) | **yes** | yes |
| Standing relaxation of a global limit | n/a | **no** (a new cap, only for a mode with no probe) | **yes** (`MAX_CAPTURE_SECONDS`) | yes |
| One evidence stream | yes | **no, two files** | **yes** | yes |
| Probe identity bound to the capture | yes | **no** | yes | yes |
| Existing assertions changed | n/a | **none** (the profile field-list pin changes by design) | **about four** (listed above) | several |
| New code | n/a | the most (a mode, a field, a cap) | the least | the most |
| Compatible with B5 by the reset button | yes | yes | yes | not needed |

**Recommendation: option X.** The reason: the one mode that writes flash stays exactly as reviewed, with its tests as the proof; the long window lives in a mode that has no way to start a probe command; no global limit is relaxed; and the cost is **bounded and procedural**: a seam that the judge's own rules suggest is benign when no TCP client starts before the second capture is open (not measured), and no probe identity in a mode that touches nothing. **Choose Y2 instead only if the owner decides that a two-file evidence stream is not acceptable**, because Y2 is the smallest change and gives one stream, at the price of editing the write path and relaxing a global cap. Z is not recommended now.

### Decisions the owner is asked to take before any implementation

| # | Decision | Recommendation |
| --- | --- | --- |
| D1 | Option X, Y2 or Z | X |
| D2 | The window (X: of `watch`; Y: of `program`) | 2,400 s; the cap 3,600 s for X, 2,400 s for Y |
| D3 | Whether the guard must cover the whole window (it does not today, G5) | **Yes**: the one check requires 90 s plus the window |
| D4 | Whether a guard re-check **during** the window is wanted (it would abort the capture, never the image, and adds checks to a window that has none today) | No: one check at the open, with the window-covering validity |
| D5 | The series budget for `watch` | 300 s plus the window |
| D6 | Accept the seam and its procedure (X only) | Yes |
| D7 | Name of the mode (X only) | `watch` |

### Review requirements for B4

These are driver and gate changes. `tests/target/` is not on the protected list of `AGENTS.md`, but the brief treats them like protected paths, and this document asks for the same care.

| Requirement | Content |
| --- | --- |
| Approval first | The owner approves D1 to D7 before implementation (Gate 1: a material technical decision). This document is not that approval. |
| Test-driven | Red before green for every new behaviour, with the red output kept; the existing tests of the gates must pass unchanged. |
| Checks, run by the author and recorded apart from the review | `python3 -m unittest discover -s tests/build -p 'test_m5_*.py'` (the command of `scripts/test-networking-physical.sh`) and `-p 'test_bench_*.py'`, host only. |
| Independent review | The `reviewer` agent (Opus), given the base and head SHAs, the diff command, the diff as a file, the changed paths, the acceptance criteria below and the check results with who ran them. A reasoned `PASS`, `REVISE` or `ESCALATE`. |
| Acceptance criteria for the reviewer | (a) No path reaches `probe_command` after a capture is open, in any option. (b) Every gate of the current `program` runs before the capture opens. (c) No existing assertion is weakened, and each changed one is quoted with its approval. (d) The new limits equal the approved numbers. (e) The pinned field and parameter lists changed only as the design says. |
| PR description | Each changed protected path from `git diff --name-only --no-renames origin/main...HEAD`, with the approval or its quote. |
| Merge and hardware | Merge is a human decision (Gate 2). A `PASS` grants neither a merge nor a board operation. **The first physical use of a longer window needs its own approval, reviewed plan and the root mount guard.** |

---

## What was not verified

| Item | State |
| --- | --- |
| The shape of the C11 `tcp_init_isn` patch, its file and hash | **Unknown.** The branch holds no change. The design is a list with placeholders. |
| The file list of the wolfSSL socket patch, and whether it uses renames, mode changes or binary hunks | **Unknown.** Only `sockets_tls.c` is hashed today. |
| Whether `git archive` converts line endings or expands attributes on the build machine, and whether `patch` behaves alike on two machines | **Unknown.** |
| Real report rates, real UART bytes per second, and how long the guard checks take | **Not measured.** The sizes above are worst-case bounds. |
| Whether the guard's deadline counts from its start or from `READY` | **Not read.** |
| The paths in `compile_commands.json` of the real container build | **Not seen.** |
| Whether the identity-only probe leaves the core running | **Unknown.** |
| Everything on hardware | Nothing ran. No test, build or command of this design was executed: it changes only a document. |
| The documentation check | `scripts/check_docs.py` needs Python 3.11 or later and the pinned lychee, and neither is available to the session that wrote this. The links and anchors of this file were checked by a small throwaway script instead, and CI runs the real check. |
