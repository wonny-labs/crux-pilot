# Stage 3 summary: HAB, XIP encryption, boot media and signing tools

Issue: wonny-labs/crux-pilot#141, Stage 3. Desk study, 1 November 2026. Claim numbers (A1, B6 and so on) refer to [stage3-findings.md](stage3-findings.md) in the same folder. The EU Cyber Resilience Act (CRA) is out of scope.

## 1. HAB in open configuration with unprogrammed SRK hash fuses

- The RT1060 behaviour itself is **Not verified**. The RT1060 security reference manual and AN12079 could not be read (A13).
- Generic HABv4 documents state that HAB authenticates the SRK table against the `SRK_HASH` fuses (A10). With blank fuses this check cannot succeed. The expected result is that open configuration can show that a CSF was found and processed, and which checks failed. It cannot show a successful authentication. This is an expectation, not an RT1060 result.
- **Reading events without fuse writes** needs a board. Possible means are: software on the target calling the HAB `report_event` and `report_status` API (as U-Boot does on i.MX6/7, A11), or the serial downloader `error-status` command (A15). The pinned RT1062 ROM API driver has no HAB entry (A8). Zephyr would need new code. All event readback is out of scope for #141 (section 1.2).
- **What events could show:** that the ROM parsed the IVT and CSF, that signature and certificate structures are well formed, and the failure reason against blank fuses. **What they cannot show:** a passing authentication, enforcement, or that a wrongly signed image is rejected.
- **Verifiable without SRK fuse writes:** offline image structure (IVT, CSF, SRK table) with `nxpimage hab parse`, and offline signature checks. **Not verifiable:** ROM acceptance of the signature, closed-configuration rejection, and boot-time behaviour.
- Vendor answers about RT1020 (A12, A16) are inputs only.

## 2. Trust level without fuse programming

- Achievable: MCUboot verifies the application image with a development key on every boot (plan Appendix A, `BOOT_VALIDATE_SLOT0`).
- The boot ROM does not authenticate the bootloader. A Zephyr-built bootloader has `csf = 0` in its IVT (A1, A2). Even a HAB-signed bootloader is not enforced in open configuration (Not verified for RT1060).
- Must be declared: no hardware root of trust. Anyone with Flash or debug access can replace the bootloader and its embedded public key. Keys are development keys. No hardware anti-rollback counter exists. HAB enforcement and BEE are not active.

## 3. Signing toolchain

| Role | Tool | Version (1 Nov 2026) | Licence and access | Dependencies | Reproducible CLI |
| --- | --- | --- | --- | --- | --- |
| Initial manual check of HAB images (optional) | SEC | 26.09, integrates SPSDK 3.12.0, RT106x supported (B10) | NXP account required. Licence name not shown (B12) | Desktop installer (Windows, macOS, Ubuntu) | No. `securep` generates scripts, but the workspace comes from the GUI (B11). GUI output is not evidence. |
| Scripted HAB bootloader signing | SPSDK `nxpimage hab` | 3.12.0 (B5) | BSD-3-Clause on PyPI. Binary dependency licences not checked (B5) | Python >=3.10, 34 required dependency entries | Yes, YAML configuration (B6). |
| Application images | MCUboot `imgtool` | Pinned in-tree 2.4.0 (B15) | Apache-2.0 | `cryptography>=40`, `intelhex`, `click`, `cbor2`, `setuptools`, `pyyaml` (B16) | Yes. Already wired into `west sign` and sysbuild (A9). |
| Legacy HAB signer | NXP CST | Debian has 4.0.1 (B14) | NXP click-through licence LA_OPT36 (B13) | Native binary | Yes, but superseded by SPSDK (B6, B10). |

- `requirements-extras.txt` with `spsdk >= 3.0.0` is a **Zephyr** file (B1). crux-pilot `origin/main` has no such file and does not install SPSDK (B2, B3). The plan's Stage 3 input row should be corrected.
- **Recommended split:** `imgtool` from the pinned MCUboot for application images (A1 trial). SPSDK `nxpimage hab` as the only scripted path for a HAB-signed bootloader, deferred until A2. SEC only as an optional manual cross-check. Do not adopt CST.
- Open point: whether SPSDK accepts a Zephyr image with its own FCB and IVT (A14).

## 4. BEE

- Key source: `BEE_KEYn_SEL` selects a software key, SW-GP2 or OTPMK (C4). NXP SDK documentation says the software key is the default and allows evaluation without fusing. The `hal_nxp` driver comment says the fuse must be set for a register-loaded key (C2). The conflict is **Not verified**. Whether the ROM applies BEE to the bootloader region without a fuse is **Not verified** (C9).
- Ciphertext is bound to the address through AES-CTR (C5). Swap or move changes the address, so an image must be re-encrypted for its execution slot. NXP's SDK supports BEE only with a customised overwrite-only MCUboot without revert (C7, C8). Upstream Zephyr MCUboot has no BEE support (C3).
- RAM load into SDRAM takes the application off XIP, so BEE does not apply to it. Confidentiality at rest would then come from MCUboot image encryption, which sysbuild allows with RAM load (C10). Protection of the decryption key remains open.

## 5. Boot from SD or eMMC

- SPSDK lists SD, MMC and eMMC boot layouts for this family, with the HAB container at 0x400 (D4). This suggests ROM support, but the RT1060 behaviour and EVKB switch settings are **Not verified** (D5). The Zephyr board doc gives only the QSPI and serial downloader SW7 settings (D3). Correction after independent review 1: the Zephyr document uses EVK (Rev A) switch names. On the EVKB the boot switch is SW4. QSPI is `OFF-OFF-ON-OFF`, Serial Downloader is `OFF-OFF-OFF-ON` and `ON-OFF-ON-OFF` selects SD card boot. SW7 is a reset button. These come from the EVKB Board User Manual Rev. 0, Tables 9 and 10, read only through a summarising fetch (Verified by public vendor document, summarising fetch, not a full read). D5 is therefore partly answered: a switch setting for SD boot exists, but whether it boots without fuse settings is still **Not verified**.
- Pinned Zephyr cannot build an SD or eMMC boot image for the bootloader (D1). Block-media boot would change the image layout (IVT at 0x400, no FCB, ROM copy to RAM). HAB still applies to the loaded image.

## 6. DCD and SDRAM

- The EVK enables DCD and SDRAM by default. The DCD in the boot header configures the SEMC before any code runs (D6, D7). Default RAM is SDRAM (D8).
- An MCUboot application has no boot header and no DCD (A6). The MCUboot image keeps the board DCD (D10), so the ROM initialises SDRAM before MCUboot and before a RAM-loaded application. Not verified by build. RT1060 RAM load needs a Crux overlay (D11).
- Correction after independent review 1: that the boot ROM runs the bootloader DCD is vendor behaviour and is **Not verified**. A build cannot show it. That RT1060 RAM load needs a Crux overlay is an **Inference**.

## 7. HAB closed configuration (later work only)

Requirements, not instructions: a dedicated board that can be bricked or locked, SRK hash fuse programming, a signed bootloader that boots with no HAB events, then the closed-configuration fuse (E1). eFuse writes are irreversible (E3, RT1060 statement Not verified). Tools would be SPSDK or SEC, used only under a separately approved procedure. MIMXRT1060-EVKB is listed as "No Longer Manufactured" (E2), so sourcing a dedicated board needs planning.

## Owner decisions needed

1. **A2 licence review** before any download: SPSDK and its binary dependencies, SEC (account and licence), CST (LA_OPT36). Default remains documentation-only.
2. Whether a later, separately approved hardware step may read HAB events in open configuration (no fuse writes).
3. Accept the plan correction: `requirements-extras.txt` is Zephyr's, not Crux's. MCUboot requirements have seven entries, not six. Correction after independent review 1: the plan already said seven in total and deferred the seventh. The only change is to name it: `pytest`.
4. Whether BEE stays out of M8, given that it needs customised MCUboot and overwrite-only.
5. Whether to plan a dedicated board for future closed-configuration work.

## Not verified

- RT1060 open-configuration HAB behaviour with blank SRK hash, and HAB event content (A13).
- HAB event readback means on RT1060, including `error-status` meaning (A8, A15).
- SPSDK handling of a Zephyr image with FCB and IVT (A14). RT1060 HAB key types.
- BEE key default and fuse needs (C2, C9).
- SD or eMMC ROM boot on EVKB without fuses (D5). Switch settings are now partly answered (see section 5).
- RT1060 fuse irreversibility and closed-configuration procedure (E1, E3).
- SEC licence, SEC headless operation, CST licence text and Debian licence (B11 to B14).
- Licences of SPSDK binary dependencies (B5). Python version in the Dev Container (B17).
- All documents listed as title-only in `stage3-findings.md`.
