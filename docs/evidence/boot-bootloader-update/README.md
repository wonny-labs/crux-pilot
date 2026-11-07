# Evidence for the #299 bootloader update and recovery investigation

This folder holds the supporting evidence for the
[decision record](../../boot-bootloader-update-investigation.md) of
[#299](https://github.com/wonny-labs/crux-pilot/issues/299). It is a desk
investigation. No build, hardware, debugger or fuse operation was run, and no
dependency checkout was altered.

| File | Content |
| --- | --- |
| [a-mcuboot-zephyr-ecosystem.md](a-mcuboot-zephyr-ecosystem.md) | MCUboot and Zephyr at the pins, and public designs for an updatable bootloader (Nordic, TF-M, STMicroelectronics). Rows A-1 to A-55. |
| [b-nxp-rom-fuses-hab.md](b-nxp-rom-fuses-hab.md) | RT1060 boot ROM recovery paths, fuses, ROM API, flash remap, vendor MCUboot port and HAB. Rows B-1 to B-83. |
| [c-emmc-expectations.md](c-emmc-expectations.md) | eMMC boot partition model, update patterns, Zephyr support, RT1060 framing. Rows C-1 to C-50. |

Row IDs are local to each file. The B rows are unrelated to the Stage 3 B rows
of the #141 record.

## Pinned revisions

| Component | Revision |
| --- | --- |
| Zephyr | `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0) |
| MCUboot | `ee39e2d694bd827ffd1bebbce2f571a9154e6ec2` |
| `hal_nxp` | `2c2f28ac333e995d7279777409817c4b4f92c1ec` |

These are the pins named in the #141 record (Draft PR
[#151](https://github.com/wonny-labs/crux-pilot/pull/151)). Other repositories
and documents were read at the revision named in each file's source table,
which is the default branch head or release at the read date. They are inputs,
not pinned dependencies.

## Evidence labels

The labels are those of the #141 record. They are never upgraded.

| Label | Meaning |
| --- | --- |
| Verified on pinned source | Checked at a cited file and line of a pinned Zephyr, MCUboot or `hal_nxp` revision. Shows what the code allows, not what a board does. |
| Verified by public vendor document | A public vendor or project document, repository or tool says so, at the revision named. It is the author's statement, not a Crux result. Evidence rows that use a summarising fetch say so. |
| Inference | Reasoned from verified rows, which the row names. |
| Not verified | Not checked, not accessible or contradicted by another source. The row gives the reason. |

Two limits apply to every file here:

- The NXP reference manual was read from a third-party-hosted copy of
  revision 3 (07/2021). NXP's own download needs a login. Authenticity and the
  current revision were not checked against nxp.com. The RT1060 Security
  Reference Manual, AN12079 and the Code Signing Tool guide were not readable.
- The JEDEC eMMC standard was not read. eMMC semantics come from Linux,
  mmc-utils, U-Boot, RAUC, SWUpdate and one vendor white paper.
