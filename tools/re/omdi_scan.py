#!/usr/bin/env python3
"""Scan Mac resource forks for OMS driver resources (OMdi/PPCC/OMdv/PROC)
and report the topology of every OMS driver found.

Usage: omdi_scan.py DIR...
Prints one block per resource fork that carries OMdi or PPCC, with the
OMdi payload hex-decoded and field-by-field, and every code resource
classified (68K code resource vs PEF container).
"""
import os
import struct
import sys


def parse(fn):
    with open(fn, "rb") as f:
        data = f.read()
    if len(data) < 16:
        return []
    data_off, map_off, data_len, map_len = struct.unpack(">IIII", data[:16])
    m = map_off
    if m + 28 > len(data):
        return []
    type_list_off = struct.unpack(">H", data[m + 24:m + 26])[0]
    name_list_off = struct.unpack(">H", data[m + 26:m + 28])[0]
    ntypes = struct.unpack(">H", data[m + type_list_off:m + type_list_off + 2])[0] + 1
    res = []
    try:
        for t in range(ntypes):
            e = m + type_list_off + 2 + t * 8
            if e + 8 > len(data):
                break
            typ = data[e:e + 4].decode("latin1")
            count = struct.unpack(">H", data[e + 4:e + 6])[0] + 1
            ref_off = struct.unpack(">H", data[e + 6:e + 8])[0]
            for r in range(count):
                re_ = m + type_list_off + ref_off + r * 12
                if re_ + 8 > len(data):
                    break
                rid, name_off, attrs = struct.unpack(">HHB", data[re_:re_ + 5])
                doff = struct.unpack(">I", b"\x00" + data[re_ + 5:re_ + 8])[0]
                if data_off + doff + 4 > len(data):
                    continue
                ln = struct.unpack(">I", data[data_off + doff:data_off + doff + 4])[0]
                body = data[data_off + doff + 4:data_off + doff + 4 + ln]
                name = ""
                if name_off != 0xFFFF:
                    no = m + name_list_off + name_off
                    if no < len(data):
                        nl = data[no]
                        name = data[no + 1:no + 1 + nl].decode("latin1")
                res.append((typ, rid, name, attrs, ln, body))
    except Exception:
        pass
    return res


def classify(b):
    if len(b) < 8:
        return "short"
    if b[:4] == b"Joy!":
        return "PEF container"
    if b[4:8] == b"Joy!":
        return "PEF container (4-byte len prefix = %d)" % struct.unpack(">I", b[:4])[0]
    if b[0] == 0x60 and b[1] == 0x0A and b[4:8] in (b"PROC", b"OMdv", b"CODE"):
        return "68K code resource (%s id 0x%04x)" % (
            b[4:8].decode("latin1"), struct.unpack(">H", b[6:8])[0])
    if b[:2] == b"\x60\x0a":
        return "68K jump-over-header code"
    return "other (%s)" % b[:8].hex()


def scan_one(path, seen):
    if not path.lower().endswith(".rsrc"):
        return seen
    res = parse(path)
    if not res:
        return seen
    types = {}
    for typ, rid, name, attrs, ln, body in res:
        types.setdefault(typ, []).append((rid, name, attrs, ln, body))
    if not (types.get("OMdi") or types.get("PPCC")):
        return seen
    print("==== %s" % path)
    for rid, name, attrs, ln, body in types.get("OMdi", []):
        print("  OMdi %d len=%d hex=%s" % (rid, ln, body.hex()))
        if ln >= 12:
            print("       id=0x%04x smart=%d menu=%d portM(+6)=%d portB(+8)=%d flags=0x%02x compat=%d" % (
                struct.unpack(">H", body[0:2])[0], body[2], body[4],
                struct.unpack(">H", body[6:8])[0], struct.unpack(">H", body[8:10])[0],
                body[10], body[11]))
    for rid, name, attrs, ln, body in types.get("PPCC", []):
        print("  PPCC %d len=%d %s" % (rid, ln, classify(body)))
    for rid, name, attrs, ln, body in types.get("OMdv", [])[:3]:
        print("  OMdv %d len=%d %s" % (rid, ln, classify(body)))
    for rid, name, attrs, ln, body in types.get("PROC", [])[:3]:
        print("  PROC %d len=%d %s" % (rid, ln, classify(body)))
    return True


def main():
    seen = False
    for root in sys.argv[1:]:
        if os.path.isfile(root):
            seen = scan_one(root, seen) or seen
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            for fn in sorted(filenames):
                if not fn.lower().endswith(".rsrc"):
                    continue
                path = os.path.join(dirpath, fn)
                seen = scan_one(path, seen) or seen
    if not seen:
        print("no OMdi/PPCC-bearing resource forks found")


if __name__ == "__main__":
    main()
