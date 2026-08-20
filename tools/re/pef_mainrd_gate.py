#!/usr/bin/env python3
"""Pre-runtime gate for the PPCC static RoutineDescriptor-main experiment.

Usage:
  pef_mainrd_gate.py CANDIDATE_PEF PRODUCTION_PEF

The checker validates the raw descriptor and reconstructs the same synthetic
section bases used by pefcheck. It intentionally fails when the special main
is code, when the descriptor pointer is not an in-section data pointer, or
when the candidate code/import/export structure changes.
"""
import hashlib
import importlib.util
import struct
import sys
from pathlib import Path


def u16(b, o): return struct.unpack_from(">H", b, o)[0]
def u32(b, o): return struct.unpack_from(">I", b, o)[0]


def sections(b):
    return [
        {"kind": b[o + 24], "off": u32(b, o + 20),
         "packed": u32(b, o + 16), "unpacked": u32(b, o + 12),
         "total": u32(b, o + 8), "default": u32(b, o + 4)}
        for i in range(u16(b, 0x20))
        for o in [0x28 + i * 0x1c]
    ]


def loader(b, secs):
    s = next(s for s in secs if s["kind"] == 4)
    o = s["off"]
    vals = [u32(b, o + i * 4) for i in range(14)]
    vals[0] = vals[0] - (1 << 32) if vals[0] & 0x80000000 else vals[0]
    vals[2] = vals[2] - (1 << 32) if vals[2] & 0x80000000 else vals[2]
    vals[4] = vals[4] - (1 << 32) if vals[4] & 0x80000000 else vals[4]
    return {"main_section": vals[0], "main_offset": vals[1],
            "init_section": vals[2], "term_section": vals[4],
            "export_hash": vals[11], "export_count": vals[13],
            "lib_count": vals[6], "import_count": vals[7],
            "reloc_count": vals[8], "reloc_offset": vals[9],
            "strings_offset": vals[10], "hash_power": vals[12]}


def loader_names(b, secs, l):
    """Return loader import names and exported (name,class,value,section) rows."""
    ls = next(s for s in secs if s["kind"] == 4)
    raw = b[ls["off"]:ls["off"] + ls["packed"]]
    def cstr(off):
        end = raw.find(b"\0", off)
        return raw[off:end].decode("latin1") if end >= 0 else "<unterminated>"
    p = 56
    libs = []
    for _ in range(l["lib_count"]):
        libs.append(cstr(u32(raw, p)))
        p += 24
    imports = []
    for _ in range(l["import_count"]):
        w = u32(raw, p); p += 4
        imports.append(cstr(w & 0x00ffffff))
    hp = l["hash_power"]
    p = l["export_hash"] + (1 << hp) * 4
    p += l["export_count"] * 4
    exports = []
    for _ in range(l["export_count"]):
        w = u32(raw, p); value = u32(raw, p + 4); sec = u16(raw, p + 8)
        exports.append((cstr(w & 0x00ffffff), w >> 24, value, sec))
        p += 10
    return (libs, imports, exports)


def unpack_data(b, s):
    root = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("pef_unpack", root / "pef_unpack.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.unpack(b[s["off"]:s["off"] + s["packed"]], s["unpacked"])[0]


def sha(b): return hashlib.sha256(b).hexdigest()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pef_mainrd_gate.py CANDIDATE_PEF PRODUCTION_PEF")
    cp, pp = map(Path, sys.argv[1:])
    cb, pb = cp.read_bytes(), pp.read_bytes()
    print(f"candidate={cp} size={len(cb)} sha256={sha(cb)}")
    print(f"production={pp} size={len(pb)} sha256={sha(pb)}")

    cs, ps = sections(cb), sections(pb)
    cl, pl = loader(cb, cs), loader(pb, ps)
    errors = []
    if loader_names(cb, cs, cl) != loader_names(pb, ps, pl):
        errors.append("loader imports/exports changed")
    for key in ("lib_count", "import_count", "hash_power", "export_count"):
        if cl[key] != pl[key]: errors.append(f"loader {key} changed")
    if cl["main_section"] < 0 or cs[cl["main_section"]]["kind"] not in (1, 2):
        errors.append("special main is not an unpacked/packed data section")
    if pl["main_section"] < 0:
        errors.append("production comparison PEF has no special main")
    if cl["main_section"] >= 0:
        ds = cs[cl["main_section"]]
        data = unpack_data(cb, ds) if ds["kind"] == 2 else cb[ds["off"]:ds["off"] + ds["packed"]]
        mo = cl["main_offset"]
        rd = data[mo:mo + 0x20]
        print(f"special-main=section {cl['main_section']} + 0x{mo:x} kind={ds['kind']}")
        print("descriptor=" + rd.hex(" "))
        if len(rd) < 0x20: errors.append("descriptor truncated")
        else:
            checks = [(0, b"\xaa\xfe", "magic AAFE"),
                      (2, b"\x07", "version 7"),
                      (3, b"\x00", "non-dispatched flags"),
                      (0x0c, struct.pack(">I", 0x0fb0), "ProcInfo 0x0FB0"),
                      (0x10, b"\x00", "record reserved"),
                      (0x11, b"\x01", "PPC ISA"),
                      (0x12, b"\x00\x04", "native/prepared/absolute flags"),
                      (0x18, b"\x00\x00\x00\x00", "record reserved2"),
                      (0x1c, b"\x00\x00\x00\x00", "selector")]
            for off, want, label in checks:
                if rd[off:off + len(want)] != want:
                    errors.append(f"{label}: got {rd[off:off+len(want)].hex()}")
            ptr_raw = u32(rd, 0x14)
            print(f"raw procDescriptor=0x{ptr_raw:08x}")
            if ptr_raw + 8 > len(data):
                errors.append("procDescriptor is not an in-section data offset")
            else:
                tv = data[ptr_raw:ptr_raw + 8]
                code_off, toc_off = u32(tv, 0), u32(tv, 4)
                code = cs[0]
                print(f"raw TVector=[code+0x{code_off:x}, data+0x{toc_off:x}]")
                if code_off >= code["unpacked"] or code_off % 4:
                    errors.append("TVector code word is outside/alignment-invalid")
                if toc_off >= len(data) or toc_off % 4:
                    errors.append("TVector TOC word is outside/alignment-invalid")
                pds = ps[pl["main_section"]]
                pdata = unpack_data(pb, pds) if pds["kind"] == 2 else pb[pds["off"]:pds["off"] + pds["packed"]]
                pmo = pl["main_offset"]
                if pmo + 8 <= len(pdata):
                    prod_code, prod_toc = u32(pdata, pmo), u32(pdata, pmo + 4)
                    if code_off != prod_code:
                        errors.append(f"handler code offset differs from production 0x{prod_code:x}")
                    print(f"production main TVector raw=[code+0x{prod_code:x}, data+0x{prod_toc:x}]")
                print(f"relocated TVector=[0x10000000+0x{code_off:x}, "
                      f"0x10000000+0x{toc_off:x}]")

    # Code and loader-facing import/export bytes must remain unchanged. The
    # data section is intentionally allowed to differ for the RD object.
    if len(cs) != len(ps): errors.append("section count changed")
    else:
        for i, (a, b) in enumerate(zip(cs, ps)):
            if i == cl.get("main_section") or i == pl.get("main_section"):
                continue
            if (a["kind"], a["unpacked"], a["packed"]) != (b["kind"], b["unpacked"], b["packed"]):
                errors.append(f"section {i} shape changed")
            elif a["kind"] == 0 and cb[a["off"]:a["off"] + a["packed"]] != pb[b["off"]:b["off"] + b["packed"]]:
                errors.append("PPC code section changed")
    if errors:
        print("GATE: FAIL")
        for e in errors: print("- " + e)
        return 1
    print("GATE: PASS (static descriptor/relocation-shape checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
