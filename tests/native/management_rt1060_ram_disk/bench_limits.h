/* The limits of the bench image that the RAM disk must hold. They are copied from the File service
 * area of the image's own source, tests/zephyr/management_rt1060_compile/src/mgmt_services.c (the
 * SSH composition's area, which the image uses), and tests/build/test_management_bench_medium.py
 * compares them with it, so a change there fails the unit test until this file follows. */
#ifndef BENCH_LIMITS_H
#define BENCH_LIMITS_H

#define BENCH_AREA_MAX_FILE_BYTES 65536u
#define BENCH_AREA_MAX_BYTES 262144u
#define BENCH_AREA_MAX_ENTRIES 16u
/* The credential store: two slot files, each CRUX_CREDENTIAL_SLOT_BYTES (credential_port.h,
 * compared by the unit test). */
#define BENCH_STORE_SLOTS 2u
#define BENCH_STORE_SLOT_BYTES 16384u
/* The directories of the medium: secure-a, secure-b and files. */
#define BENCH_DIRECTORIES 3u
/* What the File service keeps beside the files and counts against the area's limit only after a
 * failed commit or at the next start: up to 32 temporary names and the sidecars
 * (docs/file-service-contract.md). The test allows eight of each, which is the reservation table's
 * size, and they are one cluster each at most. */
#define BENCH_COMMIT_LEFTOVERS 16u

#endif
