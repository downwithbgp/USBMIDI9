#!/usr/bin/env python3
"""Decode a PEF loader-info section (verified 14xU32 layout) and print it.

Usage: pef_loaderinfo.py FILE
  FILE - raw PEF container (e.g. pefcheck/fixtures/*.pef)

Layout (byte-verified against authentic CodeWarrior builds, see
docs/re/pef-cfm.md and pefcheck/src/pef.rs):
  - container header 0x28 bytes; section headers at 0x28, 0x1C each;
    section count = u16 at 0x20
  - loader-info section = 14 x u32 (fixed 56 bytes): mainSection
    (SInt32, -1 = none), mainOffset, initSection, initOffset,
    termSection, termOffset, importedLibraryCount,
    totalImportedSymbolCount, relocSectionCount, relocInstrOffset,
    loaderStringsOffset, exportHashOffset, exportHashTablePower,
    exportedSymbolCount
  - export hash table: 2^power slots (u32: count<<18 | firstKeyIndex),
    then `count` keys (u32: nameLen<<16 | hash), then `count` entries of
    10 bytes (canonical u32: class<<24 | nameOffset; value u32;
    sectionIndex u16)

Promoted from /tmp/pef_loaderinfo.py; offsets verified against the
authentic TM PPCC and OMSLib 601 fixtures.
"""
import sys


def u32(d, o):
    return int.from_bytes(d[o:o + 4], "big")


def u16(d, o):
    return int.from_bytes(d[o:o + 2], "big")


def main():
    path = sys.argv[1]
    data = open(path, "rb").read()
    nsec = u16(data, 0x20)
    secs = []
    for i in range(nsec):
        o = 0x28 + i * 0x1C
        name = u32(data, o)
        default = u32(data, o + 4)
        total = u32(data, o + 8)
        unpacked = u32(data, o + 12)
        packed = u32(data, o + 16)
        coff = u32(data, o + 20)
        kind = data[o + 24]
        secs.append((kind, coff, packed, unpacked, total))
        print("sec%d kind=%d container=0x%x packed=%d unpacked=%d total=%d"
              % (i, kind, coff, packed, unpacked, total))
    loader = [s for s in secs if s[0] == 4][0]
    lo = loader[1]
    L = lo

    def l_u32(off):
        return u32(data, L + off)

    ms = l_u32(0x00)
    mo = l_u32(0x04)
    ins = l_u32(0x08)
    ino = l_u32(0x0C)
    ts = l_u32(0x10)
    to = l_u32(0x14)
    # main/init/termSection are SInt32 (-1 = none; verified layout).
    if ms >= 0x80000000:
        ms -= 0x100000000
    if ins >= 0x80000000:
        ins -= 0x100000000
    if ts >= 0x80000000:
        ts -= 0x100000000
    nlibs = l_u32(0x18)
    nimports = l_u32(0x1C)
    nrelocs = l_u32(0x20)
    reloc_off = l_u32(0x24)
    strings_off = l_u32(0x28)
    exp_hash_off = l_u32(0x2C)
    exp_power = l_u32(0x30)
    exp_count = l_u32(0x34)
    print("mainSection=%d mainOffset=0x%x initSection=%d initOffset=0x%x"
          % (ms, mo, ins, ino))
    print("termSection=%d termOffset=0x%x libs=%d imports=%d relocSections=%d"
          % (ts, to, nlibs, nimports, nrelocs))
    print("relocInstrOff=0x%x stringsOff=0x%x exportHashOff=0x%x power=%d count=%d"
          % (reloc_off, strings_off, exp_hash_off, exp_power, exp_count))
    p = L + exp_hash_off
    print("--- export hash slots (2^%d) ---" % exp_power)
    for i in range(1 << exp_power):
        w = u32(data, p + 4 * i)
        cnt = w >> 18
        idx = w & 0x3FFFF
        print("  slot%d word=0x%08x count=%d firstKeyIndex=%d" % (i, w, cnt, idx))
    p += (1 << exp_power) * 4
    print("--- export keys (%d) @0x%x ---" % (exp_count, p))
    for i in range(exp_count):
        w = u32(data, p + 4 * i)
        nl = w >> 16
        hv = w & 0xFFFF
        print("  key%d word=0x%08x nameLen=%d hash=0x%x" % (i, w, nl, hv))
    p += exp_count * 4
    print("--- export entries (%d) @0x%x ---" % (exp_count, p))
    for i in range(exp_count):
        e = p + i * 10
        can = u32(data, e)
        val = u32(data, e + 4)
        si = u16(data, e + 8)
        cls = can >> 24
        noff = can & 0xFFFFFF
        name = data[L + strings_off + noff:].split(b"\x00")[0]
        print("  entry%d @0x%x class=%d nameOffset=0x%x name=%r value=0x%x section=%d"
              % (i, e, cls, noff, name, val, si))
    print("--- loader strings @0x%x ---" % (L + strings_off))
    s = L + strings_off
    print(" ", data[s:s + 32])
    print("--- reloc instrs @0x%x ---" % (L + reloc_off))
    print(" ", data[L + reloc_off:L + reloc_off + 16].hex(" "))


if __name__ == "__main__":
    main()
