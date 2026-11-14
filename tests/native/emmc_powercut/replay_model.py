# SPDX-License-Identifier: Apache-2.0
"""Power-cut model over an eMMC simulator trace.

A trace is the simulator's record of writes, flushes, cache control and workload marks, in order.
It is expanded here to one event per sector, because one command can carry many. A position is a
cut point: the events before it were issued and the rest were not. The medium at a cut is the
baseline image, plus every write before the last barrier, plus the writes after it that the model
lets survive. A barrier is a flush that is not ignored, or the cache being turned on, before which
writes reached the medium directly. A write is only ever applied whole, except for the torn sector.

This is a model of a device, not a device: sector damage outside the write being made is not
modelled, and results say how a file system behaves against it, not how it behaves on power loss.
"""
from collections import namedtuple
import random
import struct

SECTOR = 512
MODELS = ('lost', 'prefix', 'reordered', 'torn')

_HEADER = struct.Struct('<4I')
_KINDS = {1: 'write', 2: 'flush', 3: 'cache_on', 4: 'mark'}

Event = namedtuple('Event', 'kind sector data mark')  # 'write' is per sector after expansion


class TraceError(ValueError):
    """The trace is cut short or holds a record this model does not know."""


def parse_trace(raw: bytes) -> list:
    events = []
    offset = 0
    while offset < len(raw):
        if len(raw) - offset < _HEADER.size:
            raise TraceError(f'a record header is cut short at byte {offset}')
        kind, sector, count, ident = _HEADER.unpack_from(raw, offset)
        offset += _HEADER.size
        if kind not in _KINDS:
            raise TraceError(f'unknown record type {kind} at byte {offset - _HEADER.size}')
        if _KINDS[kind] == 'write':
            size = count * SECTOR
            if count == 0 or len(raw) - offset < size:
                raise TraceError(f'the write record at byte {offset - _HEADER.size} is cut short')
            for i in range(count):
                start = offset + i * SECTOR
                events.append(Event('write', sector + i, bytes(raw[start:start + SECTOR]), None))
            offset += size
        elif _KINDS[kind] == 'mark':
            events.append(Event('mark', None, None, ident))
        else:
            events.append(Event(_KINDS[kind], None, None, None))
    return events


def mandatory_positions(events, metaroot_sectors=frozenset()) -> list:
    """Each side of every flush and of every write to a metaroot sector."""
    positions = set()
    for index, event in enumerate(events):
        if event.kind == 'flush' or (event.kind == 'write' and event.sector in metaroot_sectors):
            positions.update((index, index + 1))
    return sorted(positions)


def acked_at(events, position: int) -> int:
    """The number of marks issued before the cut, which is the number of acknowledged steps."""
    return sum(1 for event in events[:position] if event.kind == 'mark')


def _window_start(events, position, ignored_flushes):
    for index in range(position - 1, -1, -1):
        kind = events[index].kind
        if (kind == 'flush' and index not in ignored_flushes) or kind == 'cache_on':
            return index + 1
    return 0


def _apply(image, event):
    start = event.sector * SECTOR
    if start + SECTOR > len(image):
        raise ValueError(f'sector {event.sector} is outside a medium of {len(image)} bytes')
    image[start:start + SECTOR] = event.data


def build_image(baseline: bytes, events, position: int, model: str, seed: int,
                ignored_flushes=frozenset(), keep_sectors=frozenset()) -> bytes:
    if model not in MODELS:
        raise ValueError(f'unknown model {model!r}')
    if not 0 <= position <= len(events):
        raise ValueError(f'position {position} is outside a trace of {len(events)} events')
    if len(baseline) % SECTOR != 0:
        raise ValueError('the baseline is not a whole number of sectors')
    for index in ignored_flushes:
        if not 0 <= index < len(events) or events[index].kind != 'flush':
            raise ValueError(f'event {index} is not a flush')

    image = bytearray(baseline)
    start = _window_start(events, position, ignored_flushes)
    for event in events[:start]:
        if event.kind == 'write':
            _apply(image, event)

    window = [event for event in events[start:position] if event.kind == 'write']
    if model == 'lost':
        survivors = []
    elif model == 'reordered':
        draw = random.Random(seed)
        survivors = [event for event in window
                     if draw.random() < 0.5 or event.sector in keep_sectors]
    else:
        survivors = window
    for event in survivors:
        _apply(image, event)

    if model == 'torn' and position < len(events) and events[position].kind == 'write':
        event = events[position]
        old = image[event.sector * SECTOR:(event.sector + 1) * SECTOR]
        if len(old) != SECTOR:
            raise ValueError(f'sector {event.sector} is outside a medium of {len(image)} bytes')
        mask = random.Random(seed).getrandbits(SECTOR)
        mixed = bytes(new if mask >> i & 1 else was
                      for i, (was, new) in enumerate(zip(old, event.data)))
        _apply(image, Event('write', event.sector, mixed, None))
    return bytes(image)



# Reliance Edge in the Crux profile (zephyr/fs/reliance_edge/config): blocks of 4 KiB, so eight
# sectors each, with block 0 the master block and the two metaroots in the blocks numbered from
# BLOCK_NUM_FIRST_METAROOT, which is 1.
BLOCK_SECTORS = 4096 // SECTOR
FIRST_METAROOT_BLOCK = 1
METAROOTS = 2


class CommitPatternError(TraceError):
    """A metaroot write is not a whole metaroot between two flushes."""


def metaroot_sectors(offset: int = 0) -> frozenset:
    """The sectors of both metaroots of a volume that starts `offset` sectors into the disk."""
    first = offset + FIRST_METAROOT_BLOCK * BLOCK_SECTORS
    return frozenset(range(first, first + METAROOTS * BLOCK_SECTORS))


def _metaroot_blocks(offset):
    first = offset + FIRST_METAROOT_BLOCK * BLOCK_SECTORS
    return [list(range(first + n * BLOCK_SECTORS, first + (n + 1) * BLOCK_SECTORS))
            for n in range(METAROOTS)]


def flushes_before_metaroot(events, offset: int = 0) -> frozenset:
    """The flush before each metaroot write: the first of the pair that brackets a commit.

    A commit is expected as a flush, every sector of one metaroot in order, then a flush. Any other
    write to a metaroot sector raises CommitPatternError, because treating the wrong flush as the
    barrier would make a barrier control test nothing.
    """
    sectors = metaroot_sectors(offset)
    blocks = _metaroot_blocks(offset)
    found = set()
    index = 0
    while index < len(events):
        event = events[index]
        if event.kind != 'write' or event.sector not in sectors:
            index += 1
            continue
        end = index
        while end < len(events) and events[end].kind == 'write' and events[end].sector in sectors:
            end += 1
        run = [e.sector for e in events[index:end]]
        if run not in blocks:
            raise CommitPatternError(f'events {index} to {end - 1} write metaroot sectors {run}, '
                                     'not one whole metaroot in order')
        if index == 0 or events[index - 1].kind != 'flush':
            raise CommitPatternError(f'the metaroot write at event {index} has no flush before it')
        if end == len(events) or events[end].kind != 'flush':
            raise CommitPatternError(f'the metaroot write at event {index} has no flush after it')
        found.add(index - 1)
        index = end
    return frozenset(found)
