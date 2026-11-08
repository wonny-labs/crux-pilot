#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests of the power-cut replay model. They need no build: traces are made here."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from replay_model import (SECTOR, CommitPatternError, TraceError, acked_at,  # noqa: E402
                          build_image, flushes_before_metaroot, mandatory_positions,
                          metaroot_sectors, parse_trace)

SECTORS = 16
MODELS = ('lost', 'prefix', 'reordered', 'torn')


def fill(value):
    return bytes([value]) * SECTOR


def write(sector, *values):
    payload = b''.join(fill(value) for value in values)
    return struct.pack('<4I', 1, sector, len(values), 0) + payload


def flush():
    return struct.pack('<4I', 2, 0, 0, 0)


def cache_on():
    return struct.pack('<4I', 3, 0, 0, 0)


def mark(ident):
    return struct.pack('<4I', 4, 0, 0, ident)


def sector_of(image, sector):
    return image[sector * SECTOR:(sector + 1) * SECTOR]


BASELINE = fill(0) * SECTORS

# Sectors 1 and 2 before the flush, 5 to 8 after it; a mark after each group.
TRACE = parse_trace(cache_on() + write(1, 0x11, 0x12) + mark(1) + flush() +
                    write(5, 0x55) + write(6, 0x66) + write(7, 0x77) + write(8, 0x88) + mark(2))
FLUSH_AT = 4
AFTER_FLUSH = (5, 6, 7, 8)
VALUES = {5: 0x55, 6: 0x66, 7: 0x77, 8: 0x88}


class ReplayModelTest(unittest.TestCase):
    def test_trace_is_expanded_to_sectors(self):
        events = parse_trace(write(3, 0xa1, 0xa2, 0xa3))
        self.assertEqual([(e.kind, e.sector, e.data) for e in events],
                         [('write', 3, fill(0xa1)), ('write', 4, fill(0xa2)),
                          ('write', 5, fill(0xa3))])
        self.assertEqual([e.kind for e in TRACE],
                         ['cache_on', 'write', 'write', 'mark', 'flush', 'write', 'write',
                          'write', 'write', 'mark'])
        self.assertEqual(TRACE[3].mark, 1)

    def test_truncated_trace_is_an_error(self):
        whole = write(3, 0xa1, 0xa2) + mark(1)
        for cut in (1, 15, 16 + SECTOR, len(whole) - 1):
            with self.assertRaises(TraceError, msg=f'cut at {cut}'):
                parse_trace(whole[:cut])
        with self.assertRaises(TraceError):
            parse_trace(struct.pack('<4I', 9, 0, 0, 0))

    def test_everything_before_the_last_flush_is_present(self):
        for model in MODELS:
            for seed in range(20):
                image = build_image(BASELINE, TRACE, len(TRACE), model, seed)
                self.assertEqual(sector_of(image, 1), fill(0x11), f'{model} seed {seed}')
                self.assertEqual(sector_of(image, 2), fill(0x12), f'{model} seed {seed}')

    def test_lost_keeps_nothing_after_the_last_flush(self):
        image = build_image(BASELINE, TRACE, len(TRACE), 'lost', 0)
        for sector in AFTER_FLUSH:
            self.assertEqual(sector_of(image, sector), fill(0))
        # Without a flush, nothing written once the cache is on survives.
        image = build_image(BASELINE, TRACE, FLUSH_AT, 'lost', 0)
        self.assertEqual(image, BASELINE)

    def test_writes_before_the_cache_is_on_are_on_the_medium(self):
        events = parse_trace(write(3, 0x33) + cache_on() + write(4, 0x44))
        image = build_image(BASELINE, events, len(events), 'lost', 0)
        self.assertEqual(sector_of(image, 3), fill(0x33))
        self.assertEqual(sector_of(image, 4), fill(0))

    def test_prefix_keeps_oldest_in_order(self):
        for written in range(len(AFTER_FLUSH) + 1):
            image = build_image(BASELINE, TRACE, FLUSH_AT + 1 + written, 'prefix', 0)
            for index, sector in enumerate(AFTER_FLUSH):
                expected = fill(VALUES[sector]) if index < written else fill(0)
                self.assertEqual(sector_of(image, sector), expected, f'{written} written')
        # A later write to the same sector wins, because the writes are applied in order.
        events = parse_trace(write(2, 0x21) + write(2, 0x22))
        self.assertEqual(sector_of(build_image(BASELINE, events, 2, 'prefix', 0), 2), fill(0x22))
        self.assertEqual(sector_of(build_image(BASELINE, events, 1, 'prefix', 0), 2), fill(0x21))

    def test_reordered_is_deterministic_by_seed(self):
        draws = {seed: build_image(BASELINE, TRACE, len(TRACE), 'reordered', seed)
                 for seed in range(8)}
        self.assertEqual(draws[3], build_image(BASELINE, TRACE, len(TRACE), 'reordered', 3))
        self.assertGreater(len(set(draws.values())), 1)
        # A draw keeps a subset, each sector whole: never a write that is not in the window.
        for image in draws.values():
            for sector in AFTER_FLUSH:
                self.assertIn(sector_of(image, sector), (fill(0), fill(VALUES[sector])))
        # Some draw keeps a later sector without an earlier one, which a prefix never does.
        self.assertTrue(any(sector_of(image, 5) == fill(0) and sector_of(image, 8) == fill(0x88)
                            for image in draws.values()))

    def test_torn_mixes_only_the_next_sector(self):
        position = FLUSH_AT + 2  # sector 5 written, sector 6 next
        prefix = build_image(BASELINE, TRACE, position, 'prefix', 0)
        torn = build_image(BASELINE, TRACE, position, 'torn', 4)
        mixed = sector_of(torn, 6)
        self.assertNotEqual(mixed, fill(0))
        self.assertNotEqual(mixed, fill(0x66))
        self.assertTrue(all(byte in (0, 0x66) for byte in mixed))
        for sector in range(SECTORS):
            if sector != 6:
                self.assertEqual(sector_of(torn, sector), sector_of(prefix, sector))
        self.assertEqual(torn, build_image(BASELINE, TRACE, position, 'torn', 4))
        self.assertNotEqual(torn, build_image(BASELINE, TRACE, position, 'torn', 5))
        # With no write next, there is nothing to tear.
        self.assertEqual(build_image(BASELINE, TRACE, FLUSH_AT, 'torn', 4),
                         build_image(BASELINE, TRACE, FLUSH_AT, 'prefix', 0))

    def test_mandatory_positions_include_both_sides_of_each_flush(self):
        self.assertEqual(mandatory_positions(TRACE), [FLUSH_AT, FLUSH_AT + 1])
        events = parse_trace(write(1, 0x11) + flush() + flush() + write(2, 0x22) + flush())
        self.assertEqual(mandatory_positions(events), [1, 2, 3, 4, 5])
        self.assertEqual(mandatory_positions(parse_trace(write(1, 0x11, 0x12))), [])

    def test_mandatory_positions_include_each_metaroot_sector(self):
        positions = mandatory_positions(TRACE, frozenset({6, 7}))
        self.assertEqual(positions, [FLUSH_AT, FLUSH_AT + 1, 6, 7, 8])
        self.assertEqual(mandatory_positions(TRACE, frozenset({15})), [FLUSH_AT, FLUSH_AT + 1])

    def test_ignored_flush_is_not_a_barrier(self):
        image = build_image(BASELINE, TRACE, len(TRACE), 'lost', 0, ignored_flushes={FLUSH_AT})
        self.assertEqual(image, BASELINE)
        seen_without_sector_1 = False
        for seed in range(16):
            image = build_image(BASELINE, TRACE, len(TRACE), 'reordered', seed,
                                ignored_flushes={FLUSH_AT})
            seen_without_sector_1 |= sector_of(image, 1) == fill(0)
        self.assertTrue(seen_without_sector_1)
        with self.assertRaises(ValueError):
            build_image(BASELINE, TRACE, len(TRACE), 'lost', 0, ignored_flushes={1})

    def test_keep_sectors_survive_a_reordered_draw(self):
        for seed in range(32):
            image = build_image(BASELINE, TRACE, len(TRACE), 'reordered', seed,
                                keep_sectors={6, 7})
            self.assertEqual(sector_of(image, 6), fill(0x66), f'seed {seed}')
            self.assertEqual(sector_of(image, 7), fill(0x77), f'seed {seed}')
        others = {build_image(BASELINE, TRACE, len(TRACE), 'reordered', seed,
                              keep_sectors={6, 7})[5 * SECTOR] for seed in range(32)}
        self.assertEqual(others, {0, 0x55})

    def test_acked_at_counts_marks_up_to_position(self):
        self.assertEqual([acked_at(TRACE, p) for p in range(len(TRACE) + 1)],
                         [0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2])

    def test_metaroot_sectors_cover_both_blocks(self):
        # Blocks 1 and 2 of 4 KiB, so sectors 8 to 23, moved by the volume's sector offset.
        self.assertEqual(metaroot_sectors(), frozenset(range(8, 24)))
        self.assertEqual(metaroot_sectors(128), frozenset(range(136, 152)))

    def test_flushes_before_metaroot_finds_the_first_of_each_pair(self):
        events = parse_trace(cache_on() + write(40, *range(1, 9)) + flush() +
                             write(8, *range(0x80, 0x88)) + flush() + mark(1) +
                             write(48, 0x30) + flush() + write(16, *range(0x90, 0x98)) +
                             flush() + mark(2))
        first, second = 9, 21  # after cache_on and 8 data sectors; after a mark and 1 sector
        self.assertEqual([events[i].kind for i in (first, second)], ['flush', 'flush'])
        self.assertEqual(flushes_before_metaroot(events), frozenset({first, second}))
        # The other flush of each pair stays a barrier.
        self.assertEqual(events[first + 9].kind, 'flush')
        self.assertNotIn(first + 9, flushes_before_metaroot(events))
        self.assertEqual(flushes_before_metaroot(parse_trace(write(40, 1) + flush())),
                         frozenset())
        moved = parse_trace(flush() + write(136, *range(8)) + flush())
        self.assertEqual(flushes_before_metaroot(moved, offset=128), frozenset({0}))

    def test_unexpected_commit_pattern_is_an_error(self):
        whole = write(8, *range(8))
        layouts = {
            'no flush before': write(40, 1) + whole + flush(),
            'no flush after': flush() + whole + write(40, 1) + flush(),
            'trace ends inside': flush() + whole,
            'part of a metaroot': flush() + write(8, *range(4)) + flush(),
            'both metaroots in one run': flush() + whole + write(16, *range(8)) + flush(),
            'out of order': flush() + write(12, *range(4)) + write(8, *range(4)) + flush(),
            'a metaroot sector twice': flush() + whole + write(15, 9) + flush(),
        }
        for name, raw in layouts.items():
            with self.assertRaises(CommitPatternError, msg=name):
                flushes_before_metaroot(parse_trace(raw))

    def test_arguments_are_checked(self):
        for position in (-1, len(TRACE) + 1):
            with self.assertRaises(ValueError):
                build_image(BASELINE, TRACE, position, 'prefix', 0)
        with self.assertRaises(ValueError):
            build_image(BASELINE, TRACE, 0, 'shuffled', 0)
        with self.assertRaises(ValueError):
            build_image(fill(0) * 4, TRACE, len(TRACE), 'prefix', 0)  # sector 8 is outside


if __name__ == '__main__':
    unittest.main()
