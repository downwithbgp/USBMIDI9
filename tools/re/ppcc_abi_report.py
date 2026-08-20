#!/usr/bin/env python3
"""Report the statically verifiable OMS PPCC main-entry ABI.

This is deliberately a report/checker, not a PEF rewriter.  It validates the
ordinary CodeWarrior shape used by the USBMIDI9 production PEF and compares it
with an authentic PPCC component fixture.  A third, optional input may be the
rejected descriptor-main experiment; it is classified as a descriptor chain
and is never accepted as a PPCC main by this tool.

Usage:
    ppcc_abi_report.py CANDIDATE.pef [--control TM.pef] [--rejected RD2.pef]

The vector words in a PEF are pre-relocation section offsets.  The report
reconstructs the same deterministic section bases used by pefcheck so that
the code and TOC coordinates are explicit without pretending they are G4
runtime addresses.
"""

from __future__ import print_function

import argparse
import hashlib
import importlib.util
import struct
import sys
from pathlib import Path


MAGIC = b"Joy!peffpwpc"
OMS_DRIVER_PROCINFO = 0x0FB0
SYNTHETIC_BASE0 = 0x10000000


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def align16(value):
    return (value + 0x0F) & ~0x0F


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_unpacker():
    path = Path(__file__).resolve().parent / "pef_unpack.py"
    spec = importlib.util.spec_from_file_location("ppcc_pef_unpack", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.unpack


def parse_sections(data):
    sections = []
    count = u16(data, 0x20)
    for index in range(count):
        offset = 0x28 + index * 0x1C
        sections.append({
            "index": index,
            "kind": data[offset + 24],
            "default": u32(data, offset + 4),
            "total": u32(data, offset + 8),
            "unpacked": u32(data, offset + 12),
            "packed": u32(data, offset + 16),
            "offset": u32(data, offset + 20),
        })
    return sections


def parse_loader(data, sections):
    loader = next(section for section in sections if section["kind"] == 4)
    offset = loader["offset"]
    words = [u32(data, offset + index * 4) for index in range(14)]
    signed = set((0, 2, 4))
    for index in signed:
        if words[index] & 0x80000000:
            words[index] -= 0x100000000
    return {
        "main_section": words[0],
        "main_offset": words[1],
        "init_section": words[2],
        "init_offset": words[3],
        "term_section": words[4],
        "term_offset": words[5],
        "libraries": words[6],
        "imports": words[7],
        "reloc_sections": words[8],
        "reloc_offset": words[9],
        "strings_offset": words[10],
        "export_hash": words[11],
        "hash_power": words[12],
        "exports": words[13],
    }


def c_string(raw, offset):
    end = raw.find(b"\0", offset)
    if end < 0:
        return "<unterminated>"
    return raw[offset:end].decode("latin1", "replace")


def loader_exports(data, sections, loader):
    section = next(section for section in sections if section["kind"] == 4)
    raw = data[section["offset"]:section["offset"] + section["packed"]]
    position = section["offset"] + loader["strings_offset"]

    hash_position = loader["export_hash"] + (1 << loader["hash_power"]) * 4
    hash_position += loader["exports"] * 4
    exports = []
    for index in range(loader["exports"]):
        entry = section["offset"] + hash_position + index * 10
        canonical = u32(data, entry)
        exports.append({
            "name": c_string(raw, loader["strings_offset"] + (canonical & 0x00FFFFFF)),
            "class": canonical >> 24,
            "value": u32(data, entry + 4),
            "section": u16(data, entry + 8),
        })
    return exports


def unpack_section(data, section, unpack):
    raw = data[section["offset"]:section["offset"] + section["packed"]]
    if section["kind"] == 2:
        return unpack(raw, section["unpacked"])[0]
    return raw


def synthetic_bases(sections):
    bases = []
    base = SYNTHETIC_BASE0
    for section in sections:
        bases.append(base)
        base = align16(base + section["total"])
    return bases


def describe_vector(data, sections, loader, unpack):
    main_section = loader["main_section"]
    if main_section < 0:
        return {"kind": "none", "main_section": main_section}
    if main_section >= len(sections):
        raise ValueError("special main section is out of range")
    section = sections[main_section]
    contents = unpack_section(data, section, unpack)
    offset = loader["main_offset"]
    if offset + 8 > len(contents):
        raise ValueError("special main is shorter than eight bytes")
    raw = contents[offset:offset + 8]
    word0, word1 = struct.unpack(">II", raw)
    result = {
        "main_section": main_section,
        "main_offset": offset,
        "section_kind": section["kind"],
        "raw": raw,
        "word0": word0,
        "word1": word1,
    }
    if raw[:2] == b"\xAA\xFE":
        result["kind"] = "descriptor-main"
        return result

    code_index = next((s["index"] for s in sections if s["kind"] == 0), None)
    data_index = next((s["index"] for s in sections if s["kind"] in (1, 2, 3, 5, 6)), None)
    if code_index is None or data_index is None:
        result["kind"] = "unknown"
        return result
    code = sections[code_index]
    data_section = sections[data_index]
    if word0 % 4 or word0 >= code["total"]:
        result["kind"] = "unknown"
        return result
    if word1 % 4 or word1 >= data_section["total"]:
        result["kind"] = "unknown"
        return result
    bases = synthetic_bases(sections)
    result.update({
        "kind": "transition-vector",
        "code_index": code_index,
        "data_index": data_index,
        "code_offset": word0,
        "toc_offset": word1,
        "code_base": bases[code_index],
        "data_base": bases[data_index],
        "entry": bases[code_index] + word0,
        "toc": bases[data_index] + word1,
        "code_bytes": data[code["offset"] + word0:code["offset"] + word0 + 32],
    })
    return result


def describe(path):
    path = Path(path)
    data = path.read_bytes()
    if data[:12] != MAGIC:
        raise ValueError("not a PowerPC PEF (expected Joy!peffpwpc)")
    sections = parse_sections(data)
    loader = parse_loader(data, sections)
    unpack = load_unpacker()
    vector = describe_vector(data, sections, loader, unpack)
    exports = loader_exports(data, sections, loader)
    return {
        "path": path,
        "data": data,
        "sections": sections,
        "loader": loader,
        "vector": vector,
        "exports": exports,
        "aafe_offsets": [
            offset for offset in range(len(data) - 1)
            if data[offset:offset + 2] == b"\xAA\xFE"
        ],
    }


def section_summary(image):
    return ", ".join(
        "{0}:kind={1},off=0x{2:x},packed={3},unpacked={4},total={5}".format(
            section["index"], section["kind"], section["offset"],
            section["packed"], section["unpacked"], section["total"])
        for section in image["sections"])


def print_image(image, label, require_vector=False):
    data = image["data"]
    loader = image["loader"]
    vector = image["vector"]
    print("[{0}]".format(label))
    print("file={0}".format(image["path"]))
    print("size={0} sha256={1}".format(len(data), sha256(data)))
    print("sections={0}".format(section_summary(image)))
    print("loader.mainSection={0} loader.mainOffset=0x{1:x}".format(
        loader["main_section"], loader["main_offset"]))
    print("loader.initSection={0} loader.termSection={1} libs={2} imports={3} exports={4}".format(
        loader["init_section"], loader["term_section"], loader["libraries"],
        loader["imports"], loader["exports"]))
    print("exports=" + ", ".join(
        "{0}:class={1},section={2},value=0x{3:x}".format(
            item["name"], item["class"], item["section"], item["value"])
        for item in image["exports"]))
    print("Joy!peffpwpc[0:12]=" + data[:12].hex(" "))
    aafe = ", ".join(
        "0x{0:x}".format(offset) for offset in image["aafe_offsets"])
    print("AAFE byte occurrences=" + (aafe if aafe else "none"))
    if vector["kind"] == "none":
        print("main-representation=NONE")
    elif vector["kind"] == "descriptor-main":
        print("main-representation=AAFE descriptor bytes=" + vector["raw"].hex(" "))
        print("descriptor-main-status=REJECTED: OMS PPCC path requires a direct PPC transition vector here")
    elif vector["kind"] == "transition-vector":
        print("main-representation=transition-vector")
        print("special-main=section {0} + 0x{1:x} raw={2}".format(
            vector["main_section"], vector["main_offset"], vector["raw"].hex(" ")))
        print("vector=[code section {0} + 0x{1:x}, data section {2} + 0x{3:x}]".format(
            vector["code_index"], vector["code_offset"], vector["data_index"], vector["toc_offset"]))
        print("synthetic-vector=[entry=0x{0:08x}, toc=0x{1:08x}]".format(
            vector["entry"], vector["toc"]))
        print("entry-bytes=" + vector["code_bytes"].hex(" "))
        if vector["code_bytes"][:4] == b"\x7c\x08\x02\xa6":
            print("entry-proof=mflr r0 PPC function prologue")
        else:
            print("entry-proof=unclassified PPC bytes")
        if require_vector:
            if vector["main_section"] < 0 or vector["kind"] != "transition-vector":
                raise ValueError("candidate does not have a valid transition-vector special main")
    else:
        print("main-representation=UNKNOWN raw={0}".format(vector["raw"].hex(" ")))
        if require_vector:
            raise ValueError("candidate special main is not a recognized transition vector")
    print()


def compare_code(left, right):
    left_code = next(section for section in left["sections"] if section["kind"] == 0)
    right_code = next(section for section in right["sections"] if section["kind"] == 0)
    left_bytes = left["data"][left_code["offset"]:left_code["offset"] + left_code["packed"]]
    right_bytes = right["data"][right_code["offset"]:right_code["offset"] + right_code["packed"]]
    return left_bytes == right_bytes, len(left_bytes), len(right_bytes)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", help="PEF whose PPCC main is being checked")
    parser.add_argument("--control", help="optional authentic PPCC control PEF")
    parser.add_argument("--rejected", help="optional descriptor-main PEF to classify")
    args = parser.parse_args(argv)

    try:
        candidate = describe(args.candidate)
        print("PPCC ABI diagnostic")
        print("OMS entry contract: pascal long main(short msg, long par1, long par2)")
        print("OMS ProcInfo: 0x{0:08x} (uppOMSDriverProcInfo)".format(OMS_DRIVER_PROCINFO))
        print("source evidence: OMSDriver.h + OMSDrvUPPs.h + tools/re/procinfo_check.c")
        print()
        print_image(candidate, "candidate", require_vector=True)

        if args.control:
            control = describe(args.control)
            print_image(control, "control")
            if control["vector"]["kind"] != "transition-vector":
                raise ValueError("control does not contain a transition-vector main")
            print("candidate/control code-section bytes identical={0}".format(
                compare_code(candidate, control)[0]))
            print()

        if args.rejected:
            rejected = describe(args.rejected)
            print_image(rejected, "rejected-experiment")
            if rejected["vector"]["kind"] != "descriptor-main":
                raise ValueError("--rejected input is not the expected AAFE descriptor-main")
            print("candidate/rejected code-section bytes identical={0}".format(
                compare_code(candidate, rejected)[0]))
            print("candidate/rejected loader-facing imports and exports are compared by pef_mainrd_gate.py")
            print()

        print("DIAGNOSTIC: PASS (candidate is function-main/vector, not descriptor-main)")
        return 0
    except (OSError, ValueError, struct.error) as error:
        print("DIAGNOSTIC: FAIL: {0}".format(error))
        return 1


if __name__ == "__main__":
    sys.exit(main())
