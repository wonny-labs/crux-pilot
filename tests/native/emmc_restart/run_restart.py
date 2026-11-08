#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Process-restart scenarios for the eMMC simulator.

The simulator's persistent state is backed by a host file, so it outlives the process, and its
cache is RAM, so it does not. A process exit stands in for nothing more than that: this is software
evidence about the model and says nothing about power loss on a device.

The unmodified stack sends no flush at the pinned Zephyr revision, so with it a sync reaches the
medium only without a cache, and on the cached profile the flush path is driven by a command the test
program sends itself. A second build of the same program with CONFIG_CRUX_MMC_CACHE_FLUSH=y, given
with --flush-exe, checks that a sync through the corrected stack reaches the medium with the cache on.
"""
import argparse
import os
import struct
import subprocess
import tempfile

SECTOR_SIZE = 512
MEDIUM_SIZE = 8192 * SECTOR_SIZE
FIRST_SECTOR = 100
SECTORS = 8
NO_CACHE = "--emmc-cache-kb=0"
CACHED = "--emmc-cache-kb=64"
TRACE_WRITE, TRACE_FLUSH, TRACE_CACHE_ON, TRACE_MARK = 1, 2, 3, 4


def run(exe, image, *options):
    """Run one process to completion and return (exit code, stdout lines, stderr)."""
    proc = subprocess.run([exe, f"--emmc-image={image}", *options], capture_output=True, text=True,
                          timeout=60)
    return proc.returncode, proc.stdout.splitlines(), proc.stderr


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def write(exe, image, profile, seed, *options):
    rc, out, err = run(exe, image, profile, "--phase=write", f"--restart-seed={seed}",
                       *options)
    expect(rc == 0 and "WROTE" in out, f"write failed: {rc} {out} {err}")


def expect_holds(exe, image, profile, seed):
    rc, out, err = run(exe, image, profile, "--phase=verify", f"--restart-seed={seed}")
    expect(rc == 0 and "VERIFIED" in out, f"seed {seed} is not on the medium: {rc} {out} {err}")


def expect_does_not_hold(exe, image, profile, seed):
    rc, out, err = run(exe, image, profile, "--phase=verify", f"--restart-seed={seed}")
    expect(rc == 3 and "MISMATCH" in out, f"seed {seed} should not read back: {rc} {out} {err}")


def read_file(path):
    with open(path, "rb") as handle:
        return handle.read()


def test_flushed_data_survives_a_process_restart(exe, workdir):
    # Without a cache, a write through the stack followed by a sync.
    image = os.path.join(workdir, "no_cache.bin")
    write(exe, image, NO_CACHE, 1, "--restart-sync")
    expect_holds(exe, image, NO_CACHE, 1)
    expect_does_not_hold(exe, image, NO_CACHE, 2)

    # With a cache, the same write followed by a real flush command.
    image = os.path.join(workdir, "cached.bin")
    write(exe, image, CACHED, 1, "--restart-sync", "--restart-flush")
    expect_holds(exe, image, CACHED, 1)
    expect_does_not_hold(exe, image, CACHED, 2)


def test_unflushed_cache_does_not_survive_exit(exe, workdir):
    image = os.path.join(workdir, "unflushed.bin")
    write(exe, image, NO_CACHE, 1, "--restart-sync")
    before = read_file(image)
    write(exe, image, CACHED, 2)
    expect(read_file(image) == before, "a write without a flush changed the image")
    expect_holds(exe, image, CACHED, 1)
    expect_does_not_hold(exe, image, CACHED, 2)


def test_image_size_mismatch_is_an_error(exe, workdir):
    image = os.path.join(workdir, "wrong_size.bin")
    for size in (0, SECTOR_SIZE, MEDIUM_SIZE - 1, MEDIUM_SIZE + SECTOR_SIZE):
        with open(image, "wb") as handle:
            handle.write(b"\xa5" * size)
        rc, out, err = run(exe, image, "--phase=verify", "--restart-seed=1")
        expect(rc != 0 and "emmc_sim" in err, f"an image of {size} bytes: {rc} {out} {err}")
        expect(read_file(image) == b"\xa5" * size, f"an image of {size} bytes was changed")

    # The size check is not refusing everything: a missing image is created at the right size.
    created = os.path.join(workdir, "created.bin")
    write(exe, created, NO_CACHE, 1)
    expect(os.path.getsize(created) == MEDIUM_SIZE, "a new image has the wrong size")


def read_trace(path):
    data = read_file(path)
    records = []
    offset = 0
    while offset < len(data):
        kind, sector, count, record_id = struct.unpack_from("<4I", data, offset)
        offset += 16
        payload = b""
        if kind == TRACE_WRITE:
            payload = data[offset:offset + count * SECTOR_SIZE]
            expect(len(payload) == count * SECTOR_SIZE, "a write record is cut short")
            offset += count * SECTOR_SIZE
        records.append((kind, sector, count, record_id, payload))
    return records


def test_trace_records_writes_flushes_and_marks(exe, workdir):
    image = os.path.join(workdir, "traced.bin")
    trace = os.path.join(workdir, "trace.bin")
    write(exe, image, CACHED, 7, f"--emmc-trace={trace}", "--restart-flush")
    records = read_trace(trace)
    expect([record[0] for record in records] == [TRACE_CACHE_ON, TRACE_WRITE, TRACE_MARK, TRACE_FLUSH],
           f"unexpected trace order: {[record[:4] for record in records]}")
    _, sector, count, _, payload = records[1]
    expect((sector, count) == (FIRST_SECTOR, SECTORS), f"write record: {sector} {count}")
    medium = read_file(image)
    start = FIRST_SECTOR * SECTOR_SIZE
    expect(payload == medium[start:start + len(payload)], "the traced data is not what was written")
    expect(records[2][3] == 7, f"mark id: {records[2][3]}")


def test_cached_sync_survives_a_process_restart_only_with_the_correction(exe, flush_exe, workdir):
    # At the pin, a sync with the cache on leaves the data in the cache, which the exit drops.
    image = os.path.join(workdir, "pinned.bin")
    write(exe, image, CACHED, 1, "--restart-sync")
    expect_does_not_hold(exe, image, CACHED, 1)

    # With the correction, the same sync flushes the cache, with no command from the test.
    image = os.path.join(workdir, "corrected.bin")
    write(flush_exe, image, CACHED, 1, "--restart-sync")
    expect_holds(flush_exe, image, CACHED, 1)
    expect_does_not_hold(flush_exe, image, CACHED, 2)


def test_corrected_sync_traces_one_flush_after_the_write(exe, flush_exe, workdir):
    del exe
    image = os.path.join(workdir, "traced.bin")
    trace = os.path.join(workdir, "trace.bin")
    write(flush_exe, image, CACHED, 5, f"--emmc-trace={trace}", "--restart-sync")
    kinds = [record[0] for record in read_trace(trace)]
    expect(kinds == [TRACE_CACHE_ON, TRACE_WRITE, TRACE_MARK, TRACE_FLUSH],
           f"unexpected trace order: {kinds}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, help="built native_sim executable")
    parser.add_argument("--flush-exe", required=True,
                        help="the same program built with CONFIG_CRUX_MMC_CACHE_FLUSH=y")
    args = parser.parse_args()

    for test in (test_flushed_data_survives_a_process_restart,
                 test_unflushed_cache_does_not_survive_exit,
                 test_image_size_mismatch_is_an_error,
                 test_trace_records_writes_flushes_and_marks):
        with tempfile.TemporaryDirectory() as workdir:
            test(args.exe, workdir)
        print(f"PASS {test.__name__}")

    for test in (test_cached_sync_survives_a_process_restart_only_with_the_correction,
                 test_corrected_sync_traces_one_flush_after_the_write):
        with tempfile.TemporaryDirectory() as workdir:
            test(args.exe, args.flush_exe, workdir)
        print(f"PASS {test.__name__}")


if __name__ == "__main__":
    main()
