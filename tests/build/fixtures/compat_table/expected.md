| Crux revision | zephyr | hal_nxp | mbedtls | tf-psa-crypto | wolfssl | wolfssh | wolfpsa | reliance-edge | dual-mod | extra-mod |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| v0.0.0-fixture | `4edf19ddec5a` (v4.4.2) | imported from `zephyr`, not pinned here | `d64b74f82b92`; optional: group `crux-secure-management` | `6345d9d62a01`; optional: group `crux-secure-management` | `1cced69c0d64` (v5.9.4-stable); optional: group `crux-secure-management` | `11846a9add6b` (v1.6.0-stable); optional: group `crux-secure-management` | `fe2acee1331c` (v5.9.4); optional: group `crux-secure-management` | `b5b4f3413090` (v3.0); optional: group `crux-reliance`; licence class fixture-class | `c1393802f822` | `781276ddcf6c` (2.0.1) |

- Commits are shortened to 12 characters. A version in brackets is the trailing comment of the `revision:` line in the manifest. It is not checked against the commit.
- Optional means inactive unless one of its groups is enabled. The manifest's `group-filter` turns off: `crux-reliance`, `crux-secure-management`.
- `zephyr` imports without a pin here: `cmsis`, `hal_nxp`.
