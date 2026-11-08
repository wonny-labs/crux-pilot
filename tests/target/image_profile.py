"""What differs between two images that share the reviewed board path. Never touches hardware.

A profile names only image facts. The safety gates of the programming and restore driver (guard check before
every command, ownership inventory, voltage check, bounded commands, no retry, identity-only probe, entry-image
verify, restore of the accepted normal image) are not part of a profile and cannot be changed through one.
"""
from dataclasses import dataclass, field
from pathlib import PurePosixPath
import re
from typing import Callable, Mapping, Optional, Tuple


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
    # An image built by west module discovery takes every active west project as a build module. Its modules are
    # pinned exactly like ``pinned`` and no other project is accepted. Empty for an image whose build names its
    # modules explicitly: there, a project that is not pinned is recorded and never part of the image.
    modules: Mapping[str, str] = field(default_factory=dict)
    module_paths: Mapping[str, str] = field(default_factory=dict)

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
        places = [PurePosixPath(self.manifest_path)] + [PurePosixPath(p) for p in self.pinned_paths.values()] + \
                 [PurePosixPath(p) for p in self.module_paths.values()]
        if any(a == b or a in b.parents or b in a.parents for i, a in enumerate(places) for b in places[i + 1:]):
            problems.append('two projects or the manifest checkout share or contain a path')
        if problems:
            raise ValueError('Inconsistent image profile %s: %s' % (self.name, '; '.join(problems)))

    def locked(self):
        """Every project that the image pins by name: the dependencies and, under module discovery, the build modules."""
        return {**self.pinned, **self.modules}, {**self.pinned_paths, **self.module_paths}
