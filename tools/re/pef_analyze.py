#!/usr/bin/env python3
"""PEF container: sections, imported libraries/symbols, exports, code map.

Usage: pef_analyze.py FILE
  FILE - raw PEF container (e.g. pefcheck/fixtures/production_usbmidi9.pef)

Layout (byte-verified against the authentic TM/601 fixtures and the
production build; matches pefcheck/src/pef.rs):
  - header 0x28 bytes; arch 'pwpc' at 8; sectionCount u16 at 0x20;
    section headers at 0x28, 0x1C each:
      nameOffset u32 | defaultAddress u32 | totalLength u32 |
      unpackedLength u32 | containerLength u32 | containerOffset u32 |
      kind u8 | shareKind u8 | alignment u8 | reservedA u8
  - the Loader section (kind 4) IS the loader-info container (its
    containerOffset points at the 14xU32 loader info header directly —
    there is NO loaderInfoOffset indirection; verified by byte dump:
    production loader @0x80 starts with mainSection=0xFFFFFFFF).
  - imported library table: loader container + 56, `libs` x 24 bytes;
    first u32 = nameOffset -> NUL-terminated name in the loader strings
  - imported symbol table: after the library table, `syms` x 4 bytes;
    u32 = flags<<24 | nameOffset -> NUL-terminated name in loader strings
  - exports: hash table at exportHashOffset (slots 2^power, keys, then
    10-byte entries: canonical u32 class<<24|nameOffset; value u32;
    sectionIndex u16)

CORRECTION (promoted from /tmp/pef_analyze.py, 2026-08-17): the /tmp
version used a 0x2C-byte section-header stride and guessed loader-info
offsets; both are NOT the verified PEF v1 layout and produced garbage.
"""
import struct
import sys


def u32(b, o):
    return struct.unpack('>I', b[o:o + 4])[0]


def u16(b, o):
    return struct.unpack('>H', b[o:o + 2])[0]


def cstr(b, o):
    end = b.index(0, o)
    return b[o:end].decode('latin1', 'replace')


def main(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'Joy!peff', 'not a PEF container'
    nsecs = u16(data, 0x20)
    ninst = u16(data, 0x22)
    print('arch=%s version=%d sections=%d inst=%d'
          % (data[8:12].decode('latin1'), u32(data, 0x0C), nsecs, ninst))
    sections = []
    for i in range(nsecs):
        o = 0x28 + i * 0x1C
        (name_off, default_addr, total, unpacked, cont_len,
         cont_off) = struct.unpack('>IIIIII', data[o:o + 24])
        kind = data[o + 24]
        sections.append((kind, name_off, default_addr, total, unpacked,
                         cont_len, cont_off))
        print('  sec%d kind=%d nameOff=0x%x addr=0x%08x total=%d unpacked=%d'
              ' contLen=%d contOff=0x%x'
              % (i, kind, name_off, default_addr, total, unpacked,
                 cont_len, cont_off))
    loader = [s for s in sections if s[0] == 4][0]
    lo = loader[6]
    nlibs = u32(data, lo + 0x18)
    ninst = u32(data, lo + 0x1C)
    reloc_off = u32(data, lo + 0x24)
    strings_off = u32(data, lo + 0x28)
    exp_hash_off = u32(data, lo + 0x2C)
    exp_power = u32(data, lo + 0x30)
    exp_count = u32(data, lo + 0x34)
    print('imported libraries (table @loader+56):')
    p = lo + 56
    for i in range(nlibs):
        noff = u32(data, p)
        print('  %r' % cstr(data, lo + strings_off + noff))
        p += 24
    print('--- imported symbols (total %d) ---' % ninst)
    for i in range(ninst):
        v = u32(data, p)
        print('  0x%08x %r' % (v, cstr(data, lo + strings_off + (v & 0xFFFFFF))))
        p += 4
    print('--- exports (hash table at loader+0x%x) ---' % exp_hash_off)
    for i in range(exp_count):
        e = lo + exp_hash_off + (1 << exp_power) * 4 + exp_count * 4 + i * 10
        can = u32(data, e)
        cls = can >> 24
        noff = can & 0xFFFFFF
        sym_off = u32(data, e + 4)
        name = cstr(data, lo + strings_off + noff)
        print('  %-32s class=%d section=%d value=0x%x'
              % (name, cls, u16(data, e + 8), sym_off))


if __name__ == '__main__':
    main(sys.argv[1])
