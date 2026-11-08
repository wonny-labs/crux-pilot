"""What differs between two images that share the reviewed board path. Never touches hardware.

A profile names only image facts, and whether the UART of the freshly started image is recorded raw and for how long
(``capture``, ``capture_seconds``: a bounded window, never a switch of a gate). The safety gates of the programming and restore driver (guard check before
every command, ownership inventory, voltage check, bounded commands, no retry, identity-only probe, entry-image
verify, restore of the accepted normal image) are not part of a profile and cannot be changed through one.
"""
from dataclasses import dataclass, field
from pathlib import PurePosixPath
import re
from typing import Callable, Mapping, Optional, Tuple

CAPTURE_MODES = ('none', 'raw')
MAX_CAPTURE_SECONDS = 300                       # the window after the start command; 300 s plus the 30 s Commander run at 115200 baud is at most about 3.8 MB


@dataclass(frozen=True)
class ImageProfile:
    name: str                                   # recorded in the frozen manifest; a manifest only verifies under its own name
    manifest_path: str                          # west path of the repository whose manifest the workspace was built from
    pinned: Mapping[str, str]                   # exact dependency revisions of this image
    pinned_paths: Mapping[str, str]             # where the build finds each pinned dependency
    check_image: Callable                       # (binary, elf, config) -> dict of verified image facts; raises ValueError
    commit_tag: bool                            # whether the first 12 characters of the source commit are embedded in the binary
    errno_suffix: Optional[str] = None          # source file for the errno map evidence; None keeps no errno evidence
    tool_scripts: Tuple[str, ...] = ()          # frozen scripts of the tool repository, besides tests/target/*.py
    # An image built by west module discovery takes every active west project that carries Zephyr module metadata
    # as a build module. The freeze cannot see which projects do, so such a profile pins its modules exactly like
    # ``pinned`` and accepts no other project. Empty for an image whose build names its modules explicitly: there,
    # a project that is not pinned is recorded and never part of the image.
    modules: Mapping[str, str] = field(default_factory=dict)
    module_paths: Mapping[str, str] = field(default_factory=dict)
    # Passive UART capture of the ``program`` mode: ``none`` keeps the reviewed M5 behaviour (the driver reads nothing),
    # ``raw`` records the port that the driver already owns from just before the load command to ``capture_seconds``
    # after it. The window is a whole number of seconds from 1 to MAX_CAPTURE_SECONDS and part of the registered profile.
    capture: str = 'none'
    capture_seconds: int = 0

    def __post_init__(self):
        problems = []
        if set(self.pinned) != set(self.pinned_paths):
            problems.append('pinned and pinned_paths name different projects')
        if set(self.modules) != set(self.module_paths):
            problems.append('modules and module_paths name different projects')
        if set(self.pinned) & set(self.modules):
            problems.append('a project is both pinned and a module')
        if any(re.fullmatch(r'[0-9a-f]{40}', str(revision)) is None for revision in self.modules.values()):
            problems.append('a module revision is not a full commit')
        if self.capture not in CAPTURE_MODES:
            problems.append('capture is not one of ' + ', '.join(CAPTURE_MODES))
        elif self.capture == 'none':
            if type(self.capture_seconds) is not int or self.capture_seconds != 0:
                problems.append('capture none has no window')
        elif type(self.capture_seconds) is not int or not 1 <= self.capture_seconds <= MAX_CAPTURE_SECONDS:
            problems.append('capture raw needs a window of 1 to %d whole seconds' % MAX_CAPTURE_SECONDS)
        places = [PurePosixPath(self.manifest_path)] + [PurePosixPath(p) for p in self.pinned_paths.values()] + \
                 [PurePosixPath(p) for p in self.module_paths.values()]
        if any(a == b or a in b.parents or b in a.parents for i, a in enumerate(places) for b in places[i + 1:]):
            problems.append('two projects or the manifest checkout share or contain a path')
        if problems:
            raise ValueError('Inconsistent image profile %s: %s' % (self.name, '; '.join(problems)))

    def locked(self):
        """Every project that the image pins by name: the dependencies and, under module discovery, the build modules."""
        return {**self.pinned, **self.modules}, {**self.pinned_paths, **self.module_paths}
