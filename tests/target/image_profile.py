"""What differs between two images that share the reviewed board path. Never touches hardware.

A profile names only image facts. The safety gates of the programming and restore driver (guard check before
every command, ownership inventory, voltage check, bounded commands, no retry, identity-only probe, entry-image
verify, restore of the accepted normal image) are not part of a profile and cannot be changed through one.
"""
from dataclasses import dataclass
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
