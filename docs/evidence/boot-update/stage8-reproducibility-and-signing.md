# Stage 8: reproducibility and signature verification (RT1060-EVKB)

| Field | Value |
| --- | --- |
| Date | Checks run on 2 November 2026 (signing check 00:38:56 UTC, private-key search 00:39:01 UTC). Recorded on 2 November 2026. |
| Approval | A1 (1 November 2026) |
| Evidence class | **Cross-compilation only (build-only)**, plus offline `imgtool verify`. Not Host test, `native_sim`, CI or physical-board evidence. |
| Label | **Verified by build** for statements read directly from the trial outputs |
| Provenance | Results archive `d8f53cd7-results.tar.gz`, SHA-256 `8e2e9839eae863b79a42a7a294a97e39f093a83c3f48d0598f9fb582f01cd3ed`. Copies of the text outputs: [reproducibility.txt](trial-inputs/reproducibility.txt), [signing.txt](trial-inputs/signing.txt), [hashes.txt](trial-inputs/hashes.txt). |
| Builds | The two [Stage 5](stage5-trial-builds.md) builds at crux-pilot `1d6ac4f`, Zephyr `684c9e8`, MCUboot `ee39e2d6`. The trial tree is not the PR base `f57ff22` (Stage 5 section 1). |

## 1. Reproducibility

Method: the same commit, the same container, the same development key and
`west build -p always` into a new, empty build directory (`repro-a-build`,
`repro-b-build`). The original and the rebuild therefore have different
build-directory paths. Source: `reproducibility.txt`.

| Image | `zephyr.bin` (unsigned) | `zephyr.elf` | Sections that differ |
| --- | --- | --- | --- |
| Build A MCUboot | Identical, `42100c44…dfb9` | Different (1,309,604 B and 1,309,260 B) | `.debug_info`, `.debug_line`, `.debug_str`. 33 of 36 sections identical. |
| Build A application | Identical, `ee0aad8f…2862095` | Different (727,320 B and 727,008 B) | Same three debug sections. 33 of 36 identical. |
| Build B MCUboot | Identical, `1a3cd4a0…182837f` | Different (1,353,912 B and 1,353,648 B) | Same three debug sections. 31 of 34 identical. |
| Build B application | Identical, `b845ee5b…bc6bd0a` | Different (687,388 B and 687,160 B) | Same three debug sections. 30 of 33 identical. |

Full hashes are in [reproducibility.txt](trial-inputs/reproducibility.txt) and
[hashes.txt](trial-inputs/hashes.txt).

| Finding | Result | Label |
| --- | --- | --- |
| Unsigned binaries | Byte-identical between original and rebuild, for MCUboot and the application, in both builds | Verified by build |
| ELF files | Differ only in `.debug_info`, `.debug_line` and `.debug_str`. The trial script reports "differing sections that are NOT debug/symbol tables: none" for all four ELF pairs. | Verified by build |
| Cause of the ELF difference | Each ELF contains its own build-directory name: 23, 21, 22 and 19 times (Build A MCUboot, Build A application, Build B MCUboot, Build B application), and the paths differ between the runs. The path is consistent with being the cause. | Consistent with the path. **Not confirmed**: no rebuild in the same path was made. |
| Plan stop condition ("hashes differ for reasons other than a documented embedded revision") | **Not triggered** for the unsigned binaries. For the ELF files the difference is limited to debug sections, which hold the build path. No embedded `git` revision difference arises, because both runs used the same commit. | Verified by build for the section comparison. The path as the cause is not confirmed (above). |

What "reproducible" covers here: two clean builds of the same commit and
inputs in the same container, with different build directories, give identical
unsigned binaries. It does not cover another container, another host, another
SDK installation or a later date. Those are **Not verified**.

## 2. Signed images

| Finding | Build A | Build B | Label |
| --- | --- | --- | --- |
| Header and payload, original against rebuild | Identical (32,880 B) | Identical (30,688 B) | Verified by build |
| TLV `SHA256` (0x10) and `KEYHASH` (0x01) | Identical | Identical | Verified by build |
| TLV `ECDSASIG` (0x22) | Different | Different | Verified by build |
| Total size, original and rebuild | 33,032 B and 33,031 B | 30,838 B and 30,839 B | Verified by build |

The one-byte size differences match the variable length of a DER-encoded ECDSA
signature (the dumps show signature TLV lengths `0x48` in Build A and `0x46` in
Build B, `*/imgtool-dumpinfo-app.txt`). That explanation is an Inference.

**Signature algorithm and determinism.** The images are signed with ECDSA
P-256. Two signatures of the same payload with the same key differ, so the
signature is randomised in this toolchain (imgtool 2.4.0 with
`cryptography==50.0.2`). This answers the open desk question about ECDSA nonces
for this tool version (Verified by build). It does not say anything about RSA-PSS
or Ed25519, which were not used. The plan's criterion does not need signature
determinism: unsigned images must reproduce byte for byte, and signed images
must verify against the documented public key.

## 3. Signature verification and the embedded key

Source: `signing.txt`. imgtool: `/opt/crux-cache/trial/mcuboot/scripts/imgtool.py`
(2.4.0) run with the trial virtual environment.

| Signed image | `imgtool verify --key dev-ecdsa-p256.pub.pem` | Image digest | Negative control (different throwaway key) |
| --- | --- | --- | --- |
| Build A original | "Image was correctly validated", exit 0 | `2707c7d8…cf5bffcc` | "No signature found for the given key", exit 1 |
| Build B original | Same, exit 0 | `25dc744c…40e747e7` | Same, exit 1 |
| Build A rebuild (`repro-a-build`) | Same, exit 0 | `2707c7d8…cf5bffcc` | Same, exit 1 |
| Build B rebuild (`repro-b-build`) | Same, exit 0 | `25dc744c…40e747e7` | Same, exit 1 |

The image digests of original and rebuild are equal, which agrees with the
identical `SHA256` TLV above.

| Key check | Result | Label |
| --- | --- | --- |
| Public key | ECDSA P-256. DER SubjectPublicKeyInfo, 91 B, SHA-256 `0aafb408cfe1de0f6cdb2e1f7f357a6bb171b6e53ea5b856321c2a8dcb7ea368`. X9.62 uncompressed point SHA-256 `c0b2225f7ab81029559b01fe1c9a14438aaaa951113eb7b8271a8e499ca80620`. PEM file SHA-256 `15127d78a4f119be73eefbf85ecae255fc5ca7f6f16856c2c6e9876521577d9c`. | Verified by build (`pubkey-sha256.txt`, `pubkey-der.txt`) |
| `KEYHASH` TLV in each signed image | `0a af b4 08 …`, the same value as the DER SHA-256 above | Verified by build (`*/imgtool-dumpinfo-app.txt`, `signing.txt`) |
| Key embedded in MCUboot | The generated `autogen-pubkey.c` of each MCUboot build is 91 B with SHA-256 `0aafb408…ea368`, and its bytes equal the DER of the public key. The DER bytes are also found inside MCUboot `zephyr.bin` (offset 51,224 in Build A, 53,407 in Build B, the same in each rebuild). | Verified by build |
| Default sample key | Not used. `CONFIG_BOOT_SIGNATURE_KEY_FILE` points to the external development key in both MCUboot builds. | Verified by build (`*/mcuboot/dot-config.txt`) |

The public key PEM file is in the results archive. It is **not** committed. The
plan allows committing the development public key only if the owner agrees
(plan section 9, item 10). Its SHA-256 values above are the documented
verification identity until then.

## 4. Private-key search

| Item | Result | Source |
| --- | --- | --- |
| Pattern | `-----BEGIN (any) PRIVATE KEY-----`, searched as text and as binary (`grep -rIl` and `grep -rla`) | `private-key-search.txt` |
| Locations | The crux-pilot checkout in the container, all six trial build directories (baseline, Build A, Build B, both rebuilds, route 1 check) and the results directory | Same |
| Result | 0 matching files in each location | Same |
| Raw private scalar | The 32-byte private scalar was also searched (never printed) in the same locations: 18,593 files scanned, 0 hits | Same |
| Author check of the extracted results | No PEM private-key header in the extracted results. The files committed under [trial-inputs/](trial-inputs/) were re-checked in this worktree with `git grep` for `PRIVATE KEY`: no match. | This record |

The search covers the container checkout at `1d6ac4f`, not this worktree. The
worktree check above covers the committed files.

## 5. Two kinds of development key

| Kind | Use | Bootloader | State |
| --- | --- | --- | --- |
| Fixed development key, outside the repository | Any later board test, so that a programmed bootloader and later signed images match | Built once with its public key, and kept with the board record | The trial key is a candidate. It sits on a shared Docker volume that two unrelated containers can read (Stage 5 section 11). If it is kept as the fixed development key, move it to a location only the trial uses. Otherwise delete it after the trial. The public key may be committed if the owner agrees. |
| Temporary CI key, generated per run | Build and signing tests in CI | Must be rebuilt in the same run with that run's public key, because MCUboot embeds exactly one key (Stage 1 B37 to B47, confirmed for this build by the `autogen-pubkey.c` check) | Not implemented. No CI job builds sysbuild images. The CI evidence layout would need review (plan section 3.2). |

Production signing and production keys are out of scope.

## 6. What remains open

| Item | Needs |
| --- | --- |
| Confirm that the build path is the only cause of the ELF debug-section differences | A rebuild in the same directory path, or a build with path prefix mapping, compared section by section |
| Reproducibility across containers, hosts or later dates | A rebuild elsewhere from the recorded inputs |
| Reproducibility at the PR base `f57ff22` or a later `main` | A rebuild at that revision |
| Committing the development public key | Owner agreement (plan section 9, item 10) |
| Where the fixed development key lives for any later board test | Owner decision. Delete or move the trial key. |
| CI temporary-key workflow and sysbuild evidence layout | Separate approval. Not part of #141. |
| HAB signing of the bootloader (SPSDK) | Not part of this stage. A2 is now given for an SPSDK download (see the decision record). No HAB-signed image exists. |
