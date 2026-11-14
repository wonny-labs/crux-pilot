# Evidence for the eMMC boot investigation

Issue [#298](https://github.com/wonny-labs/crux-pilot/issues/298). Desk evidence only. No build, hardware, fuse or debugger
operation was part of it. The decision record is [boot-emmc-investigation.md](../../boot-emmc-investigation.md).

| File | Content | Labels used |
| --- | --- | --- |
| [pinned-source-findings.md](pinned-source-findings.md) | Zephyr, MCUboot and `hal_nxp` source at the pins of [#141](https://github.com/wonny-labs/crux-pilot/issues/141) and at the pins of this repository's `west.yml` | Verified on pinned source, Inference, Not verified |
| [vendor-documents.md](vendor-documents.md) | NXP and SPSDK public documents on RT1060 SD and eMMC boot, and what could not be retrieved | Verified by public vendor document, Other device, Not verified |
| [emmc-semantics-and-hardware.md](emmc-semantics-and-hardware.md) | Linux, U-Boot, mmc-utils and Zephyr behaviour for eMMC boot partitions, write protection and cache, and hardware options | Verified on source, Verified by public document, Third-party statement, Inference, Not verified |

Common limits:

- Pages on `nxp.com/docs` and `jedec.org` could not be read from the environment. The JEDEC eMMC standard is not a source of
  any row. The RT1060 reference manual was read in a **third-party-hosted copy of Rev. 3**, downloaded with the owner's approval,
  and is a source of rows R1 to R12 of `vendor-documents.md` only. That copy was not compared with `nxp.com`.
- Community forum threads returned HTTP 403. They are not used as evidence.
- Row identifiers (P, V, E, H, X, Y) are local to each file.
- Each file says which rows were re-read by the author of the record and which were read by a research subagent only.
