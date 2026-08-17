#!/usr/bin/env python3
"""Parse a Mac OS resource fork and list/extract resources.

Usage:
  rsrc_list.py FILE                 list all resources
  rsrc_list.py FILE TYPE [ID]       extract resource data to stdout

Promoted from /tmp/rsrc_list.py (2026-08-17 OMS RE session); used to
list/extract the OMS driver file's fork (OMdi 128, PPCC 1, SICN 128,
vers 1) and the OMS 2.3.8 library/Setup forks.
"""
import struct
import sys


def parse(fn):
    with open(fn, "rb") as f:
        data = f.read()
    if len(data) < 16:
        sys.exit("too small")
    data_off, map_off, data_len, map_len = struct.unpack(">IIII", data[:16])
    m = map_off
    type_list_off = struct.unpack(">H", data[m + 24:m + 26])[0]
    name_list_off = struct.unpack(">H", data[m + 26:m + 28])[0]
    ntypes = struct.unpack(">H", data[m + type_list_off:m + type_list_off + 2])[0] + 1
    res = []
    for t in range(ntypes):
        e = m + type_list_off + 2 + t * 8
        typ = data[e:e + 4].decode("latin1")
        count = struct.unpack(">H", data[e + 4:e + 6])[0] + 1
        ref_off = struct.unpack(">H", data[e + 6:e + 8])[0]
        for r in range(count):
            re_ = m + type_list_off + ref_off + r * 12
            rid, name_off, attrs = struct.unpack(">HHB", data[re_:re_ + 5])
            doff = struct.unpack(">I", b"\x00" + data[re_ + 5:re_ + 8])[0]
            ln = struct.unpack(">I", data[data_off + doff:data_off + doff + 4])[0]
            body = data[data_off + doff + 4:data_off + doff + 4 + ln]
            name = ""
            if name_off != 0xFFFF:
                no = m + name_list_off + name_off
                nl = data[no]
                name = data[no + 1:no + 1 + nl].decode("latin1")
            res.append((typ, rid, name, attrs, ln, body))
    return res


def main():
    fn = sys.argv[1]
    res = parse(fn)
    if len(sys.argv) == 2:
        for typ, rid, name, attrs, ln, _ in res:
            nm = (" name=%r" % name) if name else ""
            print("%s %d attrs=0x%02x len=%d%s" % (typ, rid, attrs, ln, nm))
        return
    typ = sys.argv[2]
    rid = int(sys.argv[3]) if len(sys.argv) > 3 else None
    for t, r, name, attrs, ln, body in res:
        if t == typ and (rid is None or r == rid):
            sys.stdout.buffer.write(body)
            return
    sys.exit("not found: %s %s" % (typ, rid))


if __name__ == "__main__":
    main()
