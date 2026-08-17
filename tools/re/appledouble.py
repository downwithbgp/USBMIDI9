#!/usr/bin/env python3
"""Parse an AppleDouble file and extract entries (esp. resource fork, entry id 2).

Usage: appledouble.py FILE [OUT]
  FILE - AppleDouble file (magic 0x00051607)
  OUT  - optional output path for the resource-fork entry (id 2)

Promoted from /tmp/disasm/appledouble.py (2026-08-15 DDK/OMS research);
used to extract resource forks from AppleDouble files where the Mac
filesystem metadata (Netatalk/AFP) carried the fork.
"""
import struct, sys

def main():
    fn = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    data = open(fn, 'rb').read()
    magic = struct.unpack('>I', data[0:4])[0]
    version = struct.unpack('>I', data[4:8])[0]
    nentries = struct.unpack('>H', data[24:26])[0]
    print(f"magic=0x{magic:08x} version=0x{version:08x} nentries={nentries}")
    if magic != 0x00051607:
        print("NOT an AppleDouble (expected magic 0x00051607)"); return
    off = 26
    entries = []
    for i in range(nentries):
        eid, eoff, elen = struct.unpack('>III', data[off:off+12])
        entries.append((eid, eoff, elen))
        off += 12
    names = {1:'data', 2:'resource-fork', 3:'real-name', 4:'comment', 8:'findinfo',
             9:'applefile', 0x8002:'afp-fileid'}
    for eid, eoff, elen in entries:
        nm = names.get(eid, f'id{eid}')
        print(f"  entry {eid} ({nm}): offset={eoff} len={elen}")
        if out and eid == 2:
            open(out, 'wb').write(data[eoff:eoff+elen])
            print(f"  -> wrote {out} ({elen} bytes)")

if __name__ == '__main__':
    main()
