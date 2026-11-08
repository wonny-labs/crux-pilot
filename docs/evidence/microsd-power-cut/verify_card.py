#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the run files of the interrupted-write test on a card mounted on the host (read-only use).

Usage: verify_card.py <mount point of the card> [results.json ...]
Each run<N> file is scanned for its leading valid 32-byte records (magic, sequence, payload, CRC-32).
With result files, the scan is compared with what the board itself reported for run<boot-1>.
"""
import json
import os
import struct
import sys
import zlib


def name(hex_name):
    return bytes.fromhex(hex_name[1:]).decode()


def record(seq):
    data = bytearray(32)
    struct.pack_into('<II', data, 0, 0x31585243, seq)
    for i in range(20):
        data[8 + i] = (seq * 31 + i * 7 + 5) & 0xFF
    struct.pack_into('<I', data, 28, zlib.crc32(bytes(data[:28])) & 0xFFFFFFFF)
    return bytes(data)


def scan(path):
    data = open(path, 'rb').read()
    valid = 0
    while (valid + 1) * 32 <= len(data) and data[valid * 32:(valid + 1) * 32] == record(valid + 1):
        valid += 1
    return valid, len(data) - valid * 32


def main():
    root = os.path.join(sys.argv[1], 'files')
    runs = {}
    for entry in os.listdir(root):
        n = name(entry)
        if n.startswith('run') and n[3:].isdigit():
            runs[int(n[3:])] = scan(os.path.join(root, entry))
    print('run files:', len(runs), 'last run:', max(runs), runs[max(runs)])
    print('runs with a tail after the last valid record:', {r: t for r, (v, t) in sorted(runs.items()) if t})
    reported = {}
    for path in sys.argv[2:]:
        for cycle in json.load(open(path))['cycles']:
            if 'last_valid' in cycle:
                reported[cycle['boot'] - 1] = (cycle['last_valid'], cycle['torn_bytes'])
    mismatches = [(r, runs[r], reported[r]) for r in sorted(reported) if r in runs and runs[r] != reported[r]]
    print('board reports compared with the card:', sum(1 for r in reported if r in runs))
    print('mismatches:', mismatches)
    return 1 if mismatches else 0


if __name__ == '__main__':
    sys.exit(main())
