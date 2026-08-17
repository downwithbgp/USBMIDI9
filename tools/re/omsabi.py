#!/usr/bin/env python3
"""Authentic OMSDevice byte-level ABI audit (mac68k alignment).

mac68k alignment (from OMS SDK OMS.h `#pragma options align=mac68k`):
1-byte types align to 1, 2-byte types align to 2, 4-byte types (long/ptr)
align to 2 (NOT 4). The struct is 2-aligned. Prints the offset of every
field and the total sizeof; the driver passes sizeof(dev) = 0xB6 = 182.

Promoted from /tmp/disasm/omsabi.py (2026-08-16 OMS RE session).
"""
fields = [
    # (name, size, align) in bytes
    ("deviceRefNum",          4, 2),   # unsigned long
    ("parentDevice",          4, 2),   # ptr
    ("siblingDevices",        4, 2),   # ptr
    ("childDevices",          4, 2),   # ptr
    ("driverSpecific[4]",     4, 1),   # uchar[4]
    ("whichOut",              2, 2),   # short
    ("ownerDriver",           4, 2),   # unsigned long (OSType)
    ("uniqueID",              2, 2),   # unsigned short
    ("obsoleteGalaxyID",      2, 2),   # short
    ("flags1",                1, 1),   # uchar
    ("parentPatcherPgm",      1, 1),
    ("patcherDfltProgram",    1, 1),
    ("flags2",                1, 1),
    ("flags3",                2, 2),   # unsigned short
    ("deviceSize",            2, 2),   # short
    ("driverOwnedUniqueID",   2, 2),   # ushort
    ("nOutputPorts",          2, 2),
    ("midiDeviceID",          2, 2),
    ("midiChannels",          2, 2),
    ("iconID",                2, 2),
    ("devName (OMSString=32)",32,1),   # uchar[32]
    ("manuf[24]",             24,1),   # OD_MAX_MANUF_LEN+1=24
    ("model[24]",             24,1),   # OD_MAX_MODEL_LEN+1=24
    ("pairedDevice",          4, 2),   # ptr
    ("minDeviceID",           1, 1),
    ("maxDeviceID",           1, 1),
    ("reserved[2]",           2, 1),
    ("manufModel8[8]",        8, 1),
    ("serPortID (2 shorts)",  4, 2),   # OMSSerPortID{short,short}
    ("locationIconID",        2, 2),   # short
    ("locationName (32)",     32,1),
]

def align_up(off, a):
    return (off + a - 1) & ~(a - 1)

print("=== Authentic OMSDevice (mac68k alignment) ===")
off = 0
entries = []
for name, size, align in fields:
    off = align_up(off, align)
    entries.append((off, name, size))
    off += size
# struct alignment = 2
size = align_up(off, 2)
for o, n, s in entries:
    print(f"  offset 0x{o:02x} ({o:3d}): {n}  size={s}")
print(f"  sizeof(OMSDevice) = {size} = 0x{size:x}")
print()
# Our driver passes devSize = 0xb6 = 182
print("driver passes sizeof(dev) = 0xb6 = 182  =>", "MATCH" if size==182 else "MISMATCH")
