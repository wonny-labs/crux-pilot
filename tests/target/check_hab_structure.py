#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the HAB boot structures of a first-stage image. A developer tool, not a Crux interface.

  check <file> --medium {nor,sd,emmc} --csf {present,absent} [--dcd {present,absent}]
        [--layout {image,container}]

It reads the IVT, the boot data, the DCD pointer and header and the CSF pointer and header of an
image file and says whether they are consistent with what the caller declares. It reads one file and
changes nothing.

THIS IS A STRUCTURE CHECK. It does NOT verify a signature, a certificate, an SRK table or a key, it
does not parse the DCD commands or the CSF commands, and it says nothing about what the boot ROM or
HAB would decide. A file with a well-formed but forged CSF passes.

The standard library only, and no access to a board, a key or a vendor tool. It complements
tests/target/inspect_boot_image.py (which reads MCUboot's image format and shares no code with this
tool) and does not edit it.

The layouts below come from sources of different strength. docs/boot-hab-status.md records the
label of each statement. Apart from the check against SPSDK 3.13.0 output from synthetic input in
tests/build/test_hab_signing.py, none of it was compared against a real image or a vendor tool.

* Repository records (Verified by public vendor document, a third-party copy of the RT1060 reference
  manual, as recorded in docs/evidence/boot-bootloader-update/b-nxp-rom-fuses-hab.md and
  docs/evidence/boot-emmc/vendor-documents.md): the IVT offset is 0x1000 for FlexSPI NOR and 0x400
  for SD and eMMC; the ROM expects a 512-byte FlexSPI NOR configuration block at offset 0 on NOR; the
  IVT carries entry, dcd, boot data, self and csf pointers and the boot data carries start, length and
  plugin; the DCD is at most 1,768 bytes; a zero csf pointer means the image carries no CSF (the
  Zephyr build writes zero).
* The author's reading of public HABv4 descriptions, not re-fetched for this tool (Inference): the
  IVT is 8 words of 4 bytes (header word, entry, reserved, dcd, boot data, self, csf, reserved); the
  header word is tag 0xD1, a big-endian 16-bit length (32) and a version (0x40 or 0x41); the boot
  data is three little-endian words (start, length, plugin); the DCD header has tag 0xD2 and the CSF
  header tag 0xD4, each followed by a big-endian 16-bit length that includes the 4-byte header; the
  pointers are absolute addresses, so a pointer maps to a file offset as pointer - self +
  IVT offset; the FlexSPI configuration block tag is the bytes "FCFB"; the boot data start is the
  address of file offset 0. A mistake that this reading shares with the tests would pass.

Exit status: 0 the structure is consistent with the declaration, 1 it is inconsistent, 2 a usage error
or an unreadable file.
"""
import argparse
import json
import struct
import sys

IVT_SIZE = 32
BOOT_DATA_SIZE = 12
STRUCT_HEADER_SIZE = 4
IVT_TAG = 0xD1
DCD_TAG = 0xD2
CSF_TAG = 0xD4
IVT_VERSIONS = (0x40, 0x41)
DCD_MAX_LENGTH = 1768
IVT_OFFSETS = {"nor": 0x1000, "sd": 0x400, "emmc": 0x400}
FCB_TAG = b"FCFB"
FCB_SIZE = 512

SIGNATURE_NOTE = (
    "This is a structure check only and NOT a signature verification: no signature, certificate, SRK "
    "table or key is read, and the DCD and CSF commands are not parsed."
)
EPILOG = (
    f"{SIGNATURE_NOTE}\n"
    "The layouts are the author's reading of public descriptions and the repository's records (see "
    "docs/boot-hab-status.md); they are checked only against SPSDK 3.13.0 output from synthetic input "
    "(tests/build/test_hab_signing.py), not against a vendor reference image or the boot ROM.\n"
    "Exit status: 0 consistent, 1 inconsistent, 2 usage error."
)


def _hex(value):
    return f"0x{value:x}"


def _struct_header(data, offset):
    """(tag, length, version) of a HAB structure header at offset, or None when it does not fit."""
    if offset < 0 or offset + STRUCT_HEADER_SIZE > len(data):
        return None
    return data[offset], struct.unpack_from(">H", data, offset + 1)[0], data[offset + 3]


LAYOUTS = ("image", "container")


def check_hab_structure(data, medium, csf, dcd=None, layout="image"):
    """Check the HAB structures in data against the declared medium, CSF and (optionally) DCD.

    layout "image" means data is a device image whose IVT sits at the medium's offset. layout
    "container" means data is a bare HAB container whose IVT is at file offset 0, as `nxpimage hab
    export` writes it (observed with SPSDK 3.13.0): the bytes before the IVT offset of the medium are
    not in the file, but the boot data length and the pointers still count them.
    """
    if medium not in IVT_OFFSETS:
        raise ValueError(f"unknown medium {medium!r}; expected one of {sorted(IVT_OFFSETS)}")
    if csf not in ("present", "absent"):
        raise ValueError(f"csf must be 'present' or 'absent', not {csf!r}")
    if layout not in LAYOUTS:
        raise ValueError(f"unknown layout {layout!r}; expected one of {list(LAYOUTS)}")
    if dcd not in (None, "present", "absent"):
        raise ValueError(f"dcd must be 'present' or 'absent' when given, not {dcd!r}")
    data = bytes(data)
    size = len(data)
    ivt_offset = IVT_OFFSETS[medium]
    # Where the IVT is in the file, and how many device-image bytes precede the file.
    file_ivt = ivt_offset if layout == "image" else 0
    prefix = ivt_offset - file_ivt
    device_size = size + prefix
    checks = []

    def add(name, status, detail):
        checks.append({"name": name, "status": status, "detail": detail})

    report = {
        "kind": "hab-structure",
        "medium": medium,
        "layout": layout,
        "file_size": size,
        "expected_ivt_offset": ivt_offset,
        "declared": {"csf": csf, "dcd": dcd},
        "signature_verified": False,
        "signature_note": SIGNATURE_NOTE,
        "ivt": {"offset": None},
        "boot_data": {"offset": None},
        "dcd": {"present": None, "offset": None, "length": None, "declared": dcd,
                "note": "DCD commands are not parsed; only the header and the length are checked."},
        "csf": {"declared": csf, "present": None, "offset": None, "length": None,
                "note": "CSF commands, keys and signatures are not parsed; the region is not "
                        "authenticated or verified."},
        "checks": checks,
    }

    def finish():
        failed = [entry for entry in checks if entry["status"] == "fail"]
        report["ok"] = not failed
        report["verdict"] = "structure consistent" if not failed else "structure inconsistent"
        return report

    # FlexSPI configuration block (NOR only).
    if layout == "container":
        add("fcb", "info", "not examined: a bare container does not hold the bytes before the IVT offset")
    elif medium == "nor":
        if data[:len(FCB_TAG)] == FCB_TAG:
            add("fcb", "ok", f"the FlexSPI configuration block tag {FCB_TAG.decode()} is at offset 0")
        else:
            add("fcb", "warn", f"no {FCB_TAG.decode()} tag at offset 0 (the {FCB_SIZE}-byte configuration "
                               "block of a NOR image); the tag value is an Inference of this tool")
    else:
        add("fcb", "info", f"not examined for {medium}: an image for this medium may carry no FlexSPI "
                           "configuration block")

    # IVT position and header.
    tag_ok = file_ivt + IVT_SIZE <= size and data[file_ivt] == IVT_TAG
    if not tag_ok:
        hints = []
        for other in sorted(set(IVT_OFFSETS.values())) if layout == "image" else ():
            if other != file_ivt and other + IVT_SIZE <= size and data[other] == IVT_TAG \
                    and data[other + 1:other + 3] == bytes([0x00, IVT_SIZE]):
                hints.append(_hex(other))
        if size < file_ivt + IVT_SIZE:
            why = f"the file is {size} bytes, too short for an IVT at {_hex(file_ivt)}"
        else:
            why = f"no IVT tag {_hex(IVT_TAG)} at offset {_hex(file_ivt)} (found {_hex(data[file_ivt])})"
        if hints:
            why += (f"; an IVT-like header is at {', '.join(hints)}, which is the offset of another medium "
                    f"(this medium, {medium}, expects {_hex(ivt_offset)})")
        add("ivt", "fail", why)
        return finish()
    add("ivt", "ok", f"IVT tag {_hex(IVT_TAG)} at the expected offset {_hex(file_ivt)} "
                     f"({layout} layout, {medium}; device IVT offset {_hex(ivt_offset)})")

    _, length, version = _struct_header(data, file_ivt)
    problems = []
    if length != IVT_SIZE:
        problems.append(f"length {length}, expected {IVT_SIZE}")
    if version not in IVT_VERSIONS:
        problems.append(f"version {_hex(version)}, expected one of {[_hex(v) for v in IVT_VERSIONS]}")
    if problems:
        add("ivt_header", "fail", "IVT header: " + "; ".join(problems))
    else:
        add("ivt_header", "ok", f"IVT header length {length}, version {_hex(version)}")

    entry, reserved1, dcd_ptr, boot_data_ptr, self_ptr, csf_ptr, reserved2 = struct.unpack_from(
        "<IIIIIII", data, file_ivt + STRUCT_HEADER_SIZE)
    report["ivt"] = {"offset": file_ivt, "device_offset": ivt_offset, "length": length, "version": version, "entry": entry,
                     "dcd_ptr": dcd_ptr, "boot_data_ptr": boot_data_ptr, "self_ptr": self_ptr,
                     "csf_ptr": csf_ptr}

    if entry == 0:
        add("entry", "warn", "the entry pointer is zero")
    else:
        add("entry", "ok", f"entry pointer {_hex(entry)}")
    if reserved1 or reserved2:
        add("reserved", "warn", f"reserved IVT words are not zero ({_hex(reserved1)}, {_hex(reserved2)})")
    else:
        add("reserved", "ok", "reserved IVT words are zero")

    base = self_ptr - file_ivt  # the address of file offset 0

    def to_offset(pointer, span):
        """File offset of an absolute pointer, or (None, reason)."""
        if pointer < base:
            return None, f"pointer {_hex(pointer)} is below the image base {_hex(base)}"
        offset = pointer - base
        if offset + span > size:
            return None, (f"pointer {_hex(pointer)} (file offset {_hex(offset)}) leaves no room for "
                          f"{span} bytes in a {size}-byte file")
        return offset, None

    regions = [("ivt", file_ivt, file_ivt + IVT_SIZE)]
    if medium == "nor" and layout == "image":
        regions.append(("fcb", 0, FCB_SIZE))

    # Boot data and self pointer.
    bd = report["boot_data"]
    bd_problems = []
    start = length_bd = plugin = None
    if boot_data_ptr == 0:
        bd_problems.append("the boot data pointer is zero")
    else:
        bd_offset, why = to_offset(boot_data_ptr, BOOT_DATA_SIZE)
        if bd_offset is None:
            bd_problems.append(why)
        else:
            start, length_bd, plugin = struct.unpack_from("<III", data, bd_offset)
            bd.update({"offset": bd_offset, "start": start, "length": length_bd, "plugin": plugin})
            regions.append(("boot_data", bd_offset, bd_offset + BOOT_DATA_SIZE))
            if length_bd == 0:
                bd_problems.append("the boot data length is zero")
            elif length_bd > device_size:
                bd_problems.append(f"the boot data length {_hex(length_bd)} is larger than the image "
                                   f"({_hex(device_size)} bytes, counting the {_hex(prefix)} bytes before a bare "
                                   "container)")
            elif length_bd < ivt_offset + IVT_SIZE + BOOT_DATA_SIZE:
                bd_problems.append(f"the boot data length {_hex(length_bd)} does not cover the IVT and "
                                   f"boot data ({_hex(ivt_offset + IVT_SIZE + BOOT_DATA_SIZE)} bytes)")
    if bd_problems:
        add("boot_data", "fail", "; ".join(bd_problems))
    else:
        add("boot_data", "ok", f"boot data at offset {_hex(bd['offset'])}: start {_hex(start)}, "
                               f"length {_hex(length_bd)}, plugin {plugin}")

    if start is not None:
        if self_ptr - ivt_offset != start:
            add("self", "fail", f"self pointer {_hex(self_ptr)} minus the IVT offset {_hex(ivt_offset)} is "
                                f"{_hex(self_ptr - ivt_offset)}, but the boot data start is {_hex(start)}; "
                                "the two fields disagree and the tool cannot tell which is wrong")
        else:
            add("self", "ok", f"self pointer {_hex(self_ptr)} = boot data start {_hex(start)} + IVT offset "
                              f"{_hex(ivt_offset)}")
    else:
        add("self", "fail", "the self pointer cannot be compared because the boot data could not be read")

    if plugin is not None:
        if plugin == 0:
            add("plugin", "ok", "plugin flag 0")
        elif plugin == 1:
            add("plugin", "info", "plugin flag 1: plug-in image, not examined further")
        else:
            add("plugin", "warn", f"plugin flag {plugin}, expected 0 or 1")

    if entry != 0 and start is not None and length_bd and not start <= entry < start + length_bd:
        add("entry_range", "warn", f"the entry pointer {_hex(entry)} is outside the image range "
                                   f"[{_hex(start)}, {_hex(start + length_bd)}); a RAM-loaded image may "
                                   "legitimately differ")
    if length_bd and 0 < length_bd <= device_size:
        if length_bd == device_size:
            add("image_length", "ok", f"the boot data length equals the image size ({device_size} bytes)")
        else:
            add("image_length", "info", f"the image is {device_size - length_bd} bytes longer than the boot "
                                        f"data length {_hex(length_bd)}")

    # DCD.
    dcd_report = report["dcd"]
    if dcd_ptr == 0:
        dcd_report["present"] = False
        if dcd == "present":
            add("dcd", "fail", "declared present, but the dcd pointer is zero")
        else:
            add("dcd", "ok", "no DCD (the dcd pointer is zero)")
    elif dcd == "absent":
        dcd_report["present"] = True
        add("dcd", "fail", f"declared absent, but the dcd pointer is {_hex(dcd_ptr)}")
    else:
        dcd_report["present"] = True
        dcd_offset, why = to_offset(dcd_ptr, STRUCT_HEADER_SIZE)
        if dcd_offset is None:
            add("dcd", "fail", why)
        else:
            dcd_tag, dcd_length, _ = _struct_header(data, dcd_offset)
            dcd_report.update({"offset": dcd_offset, "length": dcd_length})
            region_end = dcd_offset + (dcd_length if dcd_length >= STRUCT_HEADER_SIZE else STRUCT_HEADER_SIZE)
            regions.append(("dcd", dcd_offset, region_end))
            dcd_problems = []
            if dcd_tag != DCD_TAG:
                dcd_problems.append(f"DCD tag {_hex(dcd_tag)}, expected {_hex(DCD_TAG)}")
            if dcd_length < STRUCT_HEADER_SIZE:
                dcd_problems.append(f"DCD length {dcd_length} is shorter than its {STRUCT_HEADER_SIZE}-byte header")
            elif dcd_length > DCD_MAX_LENGTH:
                dcd_problems.append(f"DCD length {dcd_length} is over the documented maximum {DCD_MAX_LENGTH}")
            if dcd_offset + dcd_length > size:
                dcd_problems.append(f"the DCD (offset {_hex(dcd_offset)}, length {dcd_length}) runs beyond "
                                    f"the {size}-byte file")
            if dcd_problems:
                add("dcd", "fail", "; ".join(dcd_problems))
            else:
                add("dcd", "ok", f"DCD header at offset {_hex(dcd_offset)}, length {dcd_length}")

    # CSF.
    csf_report = report["csf"]
    if csf_ptr == 0:
        csf_report["present"] = False
        if csf == "present":
            add("csf", "fail", "declared present, but the csf pointer is zero (the image carries no CSF)")
        else:
            add("csf", "ok", "declared absent, and the csf pointer is zero")
    else:
        csf_report["present"] = True
        if csf == "absent":
            add("csf", "fail", f"declared absent, but the csf pointer is {_hex(csf_ptr)}")
        else:
            csf_offset, why = to_offset(csf_ptr, STRUCT_HEADER_SIZE)
            if csf_offset is None:
                add("csf", "fail", why)
            else:
                csf_tag, csf_length, _ = _struct_header(data, csf_offset)
                csf_report.update({"offset": csf_offset, "length": csf_length})
                csf_problems = []
                if csf_tag != CSF_TAG:
                    csf_problems.append(f"no CSF header at offset {_hex(csf_offset)}: tag {_hex(csf_tag)}, "
                                        f"expected {_hex(CSF_TAG)} (a reserved but empty region?)")
                    region_len = STRUCT_HEADER_SIZE
                else:
                    region_len = csf_length if csf_length >= STRUCT_HEADER_SIZE else STRUCT_HEADER_SIZE
                    if csf_length < STRUCT_HEADER_SIZE:
                        csf_problems.append(f"CSF length {csf_length} is shorter than its "
                                            f"{STRUCT_HEADER_SIZE}-byte header")
                    if csf_offset + csf_length > size:
                        csf_problems.append(f"the CSF (offset {_hex(csf_offset)}, length {csf_length}) runs "
                                            f"beyond the {size}-byte file")
                regions.append(("csf", csf_offset, csf_offset + region_len))
                if csf_problems:
                    add("csf", "fail", "; ".join(csf_problems))
                else:
                    add("csf", "ok", f"CSF header at offset {_hex(csf_offset)}, length {csf_length}")
                    if length_bd and csf_offset + prefix >= length_bd:
                        add("csf_range", "warn", f"the CSF starts at offset {_hex(csf_offset)}, outside the "
                                                 f"boot data length {_hex(length_bd)}; the ROM copies only "
                                                 "that many bytes on SD and eMMC, so this CSF may never be "
                                                 "loaded (Inference). It does not decide the verdict")
                    elif length_bd:
                        add("csf_range", "ok", "the CSF lies inside the boot data length")

    overlaps = []
    for index, (name_a, start_a, end_a) in enumerate(regions):
        for name_b, start_b, end_b in regions[index + 1:]:
            if start_a < end_b and start_b < end_a:
                overlaps.append(f"{name_a} [{_hex(start_a)}, {_hex(end_a)}) and {name_b} "
                                f"[{_hex(start_b)}, {_hex(end_b)})")
    if overlaps:
        add("regions", "fail", "overlapping structures: " + "; ".join(overlaps))
    else:
        add("regions", "ok", "the IVT, boot data, DCD and CSF regions do not overlap")

    return finish()


def format_report(report, name="image"):
    lines = [f"HAB structure check: {name}",
             f"medium {report['medium']}, layout {report['layout']}, expected IVT offset {_hex(report['expected_ivt_offset'])}, "
             f"CSF declared {report['declared']['csf']}, DCD declared "
             f"{report['declared']['dcd'] or 'not declared'}, file size {report['file_size']} bytes"]
    for entry in report["checks"]:
        lines.append(f"  [{entry['status']:>4}] {entry['name']}: {entry['detail']}")
    lines.append(SIGNATURE_NOTE)
    lines.append(f"Result: {report['verdict']}")
    return "\n".join(lines)


def build_parser():
    parser = argparse.ArgumentParser(
        prog="check_hab_structure.py",
        description="Check the HAB structures (IVT, boot data, DCD, CSF) of a first-stage image file.",
        epilog=EPILOG, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check", help="check one image file", epilog=EPILOG,
                           formatter_class=argparse.RawDescriptionHelpFormatter)
    check.add_argument("file")
    check.add_argument("--medium", required=True, choices=sorted(IVT_OFFSETS),
                       help="boot medium: sets the expected IVT offset (nor 0x1000, sd and emmc 0x400)")
    check.add_argument("--layout", choices=LAYOUTS, default="image",
                       help="image: a device image with the IVT at the medium's offset (default); container: "
                            "a bare HAB container with the IVT at file offset 0, as nxpimage hab export writes it")
    check.add_argument("--csf", required=True, choices=("present", "absent"),
                       help="whether the image is declared to carry a CSF")
    check.add_argument("--dcd", choices=("present", "absent"),
                       help="whether the image is declared to carry a DCD (default: report what is found)")
    check.add_argument("--json", action="store_true", help="print the report as JSON")
    return parser


def main(argv=None):
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as stop:
        return stop.code if isinstance(stop.code, int) else 2
    try:
        with open(args.file, "rb") as stream:
            data = stream.read()
    except OSError as error:
        print(f"error: cannot read {args.file}: {error.strerror or error}", file=sys.stderr)
        return 2
    report = check_hab_structure(data, args.medium, args.csf, dcd=args.dcd, layout=args.layout)
    print(json.dumps(report, indent=2) if args.json else format_report(report, args.file))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
