"""Read-only candidate final-image gate; no programmer or hardware imports."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'flash_safety'))
from image_bounds import BASE, ELF_HEADER, PROGRAM_HEADER, check_image


def verify(elf, binary):
    result = check_image(elf, binary)
    header = ELF_HEADER.unpack_from(elf)
    for index in range(header[10]):
        kind, offset, vma, physical, size, memory, flags, align = PROGRAM_HEADER.unpack_from(
            elf, header[5] + index * header[9])
        if kind == 1 and size:
            if elf[offset:offset + size] != binary[physical - BASE:physical - BASE + size]:
                raise ValueError('ELF/BIN load contents differ')
    result.update(elf_sha256=hashlib.sha256(elf).hexdigest(),
                  bin_sha256=hashlib.sha256(binary).hexdigest(),
                  limitation='candidate image gate only; not execution closure or programmer protection')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--bin', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = verify(args.elf.read_bytes(), args.bin.read_bytes())
    except (OSError, ValueError, struct.error) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
