# Clean rebuild of the committed vehicle (source `a0ef59db`)

Same commands as the first build (`../commands/zbuild_wolf.sh`, Dev Container, `-j2`, a fresh patched Zephyr copy), on the committed tree after the reformat and the refusal in `main()`.

| Item | First build (sources of `vehicle-source-sha256-at-build.txt`) | This build (committed tree) |
| --- | --- | --- |
| FLASH | 601,172 B | **601,252 B** (+80 B) |
| RAM (SDRAM) | 833,708 B | 833,708 B |
| ITCM, DTCM, OCRAM, OCRAM2 | 0 B | 0 B |
| `size`: text, data, bss | 596,672, 4,480, 829,154 | 596,748, 4,480, 829,154 |
| Resolved `.config` | | identical to `../integrated-vehicle/config.txt` (compared without comments) |
| Warnings | 3, all in wolfSSH `wolfsftp.c` | the same 3 |
| `zephyr.bin` SHA-256 | `c01ac20308a809374824e49ee43d5d3713e7cce5913c1e2eb8dc6600c95cb07a` | `3af5b4644a1f178269d2682363ebda17090ec2b7909ff604ef96ea27bffa522f` |
| `zephyr.elf` SHA-256 | `00d0f2aa02e5cd78b8f86e78cf32e80055b9ce3e59b4699fa6387312738a63d4` | `5c9c04c3f6c91ac77b88ac4b093ac6e605a375c276d11b3108b3507fe2d7d607` |

The map, the static frames and the chains in `../integrated-vehicle/` are from the first build and were not repeated: the 80 B difference is the refusal's flag, its branch and its string.
