# D3 Configuration candidate results

Baseline: Crux `be0b252d78e1640bbebddefb4a6dd92fab563003` plus uncommitted
candidate changes. Zephyr `684c9e8f32e4373a21098559f748f06915f950c9`.
Container `crux-m4-config-zephyr`, cached host toolchain, native_sim/native/64.
Source and upstream checkout mounted read-only. No network/device access used.

Environment for build commands:
`PATH=/opt/crux-cache/venv/bin:$PATH ZEPHYR_BASE=/ws/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host`.

Commands executed:

```
west build -b native_sim/native/64 /ws/.worktrees/flash-safety-poc/tests/native/configuration_settings -d /tmp/m4-config-provider
/tmp/m4-config-provider/zephyr/zephyr.exe
west build -d /tmp/m4-config-provider
/tmp/m4-config-provider/zephyr/zephyr.exe
```

- RED: real fixture with linkable read/write placeholders returning NOT_SUPPORTED;
  actual Settings initialization succeeded. Five behavioral tests failed, zero
  passed, as expected. `red.log` retains individual assertions; missing load,
  persistence and error semantics were absent. No build failure is counted as RED.
- GREEN: all six final ztests passed, zero failures/skips. BOOL/U32 explicit
  roundtrip/remount, trustworthy absence after healthy traversal, missing lookup
  with failed traversal retaining active99, complete malformed values, full CRC
  errors before/after remount, observable save failure, interrupted ATE write,
  remount retaining old42 and successful retry99. Additional fault sweep injects
  one EIO at each of 13 actual Flash reads during a load; each returns IO and
  preserves active99, including traversal after matching callback.
- Intentional CRC corruption produces two expected NVS CRC error messages in
  `native.log`; these are the asserted faults, not unexplained build warnings.
- Owned C/header files passed cached clang-format dry-run with Werror.
- Recompiled the real provider command with its explicit simulator opt-in define
  removed: compilation rejected with the intended guard diagnostic (`guard.log`).
- `git diff --check` passed at this stage.

No existing unrelated build tree or process was removed. No host suite/capacity
replay/target build/hardware check was run by this subtask; integration regression
checks and independent review belong to the parent task. The native 32-bit board
variant and Twister invocation were not executed. Full `.config`, copied-source
hashes, owned-source hashes and build/test logs are retained here. All source
replacement remains isolated to this fixture. No production module wiring, commit,
push, merge, adoption or issue closure was performed.

## Independent-review corrections and final result

Final result supersedes the earlier six-case run: **10/10 native ztests pass**
(`review-final.log`), and **2/2 manifest destination regressions pass**
(`manifest-green.log`). The original six cases remain included and pass.

1. The additional `test_mount_all_open_sector_comparison_read_error` reproduced
   the review finding with the real NVS engine and one-shot EIO at Flash offset
   1114096, the last sector's preceding ATE after 16 closing-ATE comparisons.
   RED with #44/#45 only: mount returned 0, 786 reads, one write, zero erases;
   six other cases passed (`late-red.log`). The separate D3
   `late-mount-read.patch` adds only `if (rc < 0) goto end` there. GREEN: mount
   returns -EIO, 17 reads, zero writes/erases, not ready; all seven then-current
   cases passed (`late-green.log`). The original #45 patch remains unchanged.
   One configuration attempt initially rejected the manually authored extra
   patch hunk; regenerated the unified diff and reran successfully. That was a
   patch-preparation failure, not an engine result.
2. Fake-upstream manifest symlink and hardlink cases both reproduced overwrite
   failures before the fix (`manifest-red.log`). Neither uses the real pinned
   checkout. Fresh-inode temporary-file creation followed by atomic replacement
   now preserves both external victims; both regressions pass. Source-copy
   destinations already use fresh-inode replacement. The generated manifest now
   also explicitly retains the original NVS source SHA and extra patch SHA.
3. Added focused callback short-count, 256/257-byte staging-boundary and ENOSPC
   cases. The callback test uses one explicitly test-only Settings source active
   only for `cfg/short`; it supplies seven actual bytes for eight advertised.
   Actual NVS returns advertised length or errno, so this malformed callback
   behavior cannot be induced through its Flash API. A temporary mutation removing
   the provider's actual-length comparison made exactly that case fail (9 pass,
   1 fail, `short-mutation.log`); restored the original comparison and all ten pass.
   No mutation is retained. The 256-byte complete malformed record selects the
   validated default; 257 bytes return IO preserving active99. An ENOSPC error
   injected below the actual NVS write maps to NO_SPACE and preserves active42;
   this is mapping coverage, not a saturation/capacity claim.

Latest commands used the same recorded environment and build directory:

```
west build -d /tmp/m4-config-provider --cmake
/tmp/m4-config-provider/zephyr/zephyr.exe
west build -d /tmp/m4-config-provider
/tmp/m4-config-provider/zephyr/zephyr.exe
python3 -m unittest discover -s tests/native/configuration_settings -p test_prepare.py
```

Latest owned-source formatting, omitted-opt-in compilation guard and
`git diff --check` also passed. No new compiler suppressions were introduced.

Settings still repairs confirmed missing records with deletes/count updates.
NVS can still repair/write/erase during other mount recovery paths despite
bad-region reinitialization being disabled. These corrections do not establish
universally read-only or safe startup. Uncertain/destructive recovery remains a
D4 adoption blocker; see the explicit limits in the validation document.
