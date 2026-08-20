#!/usr/bin/env python3
"""Generate self-contained fixtures for the classic-Mac format tools.

Outputs (into tools/re/fixtures/):
  hqx_smoke.hqx     a real BinHex 4.0 file (encoded by this script)
  sit5_smoke.sit    a minimal SIT5 archive container with one method-0
                    entry carrying a tiny resource fork (built by this
                    script; the fork is an OMdi-bearing resource fork)
  omdi_smoke.rsrc   a minimal resource fork with OMdi 128 + OMdv 128

All bytes are generated here — no proprietary content.
"""
import binascii
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures")

ALPHA = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"
VAL2CHAR = {v: c for v, c in enumerate(ALPHA)}


def rle_encode(data):
    """BinHex RLE: [X][0x90][N] = X repeated N times TOTAL (count includes
    the byte already written); [0x90] literal = [0x90][0x00]. Applied to
    the forks BEFORE 6-bit packing; CRCs run over the RLE-expanded stream."""
    out = bytearray()
    i = 0
    while i < len(data):
        j = i
        while j + 1 < len(data) and data[j + 1] == data[i] and j - i < 254:
            j += 1
        run = j - i + 1
        if data[i] == 0x90:
            for _ in range(run):
                out.append(0x90)
                out.append(0x00)
        elif run >= 3:
            out.append(data[i])
            out.append(0x90)
            out.append(run)
        else:
            out.extend(data[i:j + 1])
        i = j + 1
    return bytes(out)


def hqx_encode(name, type_, creator, flags, data_fork, rsrc_fork):
    name = name.encode("latin1")
    """BinHex 4.0 encoder (layout A per Peter N Lewis)."""
    n = len(name)
    header = bytes([n]) + name + b"\x00" + type_ + creator + struct.pack(
        ">HII", flags, len(data_fork), len(rsrc_fork))
    raw = header + binascii.crc_hqx(header, 0).to_bytes(2, "big")
    raw += rle_encode(data_fork)
    # stored CRC is over the ORIGINAL fork; the decoder's accumulated check
    # reduces to the fork CRC via the CRC residue property
    raw += binascii.crc_hqx(data_fork, 0).to_bytes(2, "big")
    raw += rle_encode(rsrc_fork)
    raw += binascii.crc_hqx(rsrc_fork, 0).to_bytes(2, "big")
    # 8-bit -> 6-bit values (MSB-first)
    bits = 0
    nbits = 0
    vals = []
    for b in raw:
        bits = (bits << 8) | b
        nbits += 8
        while nbits >= 6:
            nbits -= 6
            vals.append((bits >> nbits) & 0x3F)
    if nbits:
        vals.append((bits << (6 - nbits)) & 0x3F)
    chars = "".join(VAL2CHAR[v] for v in vals)
    lines = [chars[i:i + 64] for i in range(0, len(chars), 64)]
    return ("(This file must be converted with BinHex 4.0)\r\n:"
            + "\r\n".join(lines) + ":\r\n").encode("latin1")


def build_resource_fork(omdi_payload, omdv_payload):
    """Minimal resource fork: OMdi 128 + OMdv 128, dataOffset=256."""
    omdi = struct.pack(">I", len(omdi_payload)) + omdi_payload
    omdv = struct.pack(">I", len(omdv_payload)) + omdv_payload
    # pad data region so the map sits at 256
    data = omdi + omdv
    map_off = 256
    # map: numTypes at +28, type list at +30, refs, then names
    nlo = 30 + 16 + 24
    names = bytearray()
    name_off_omdi = len(names)  # OMdi has no name
    names += b"\x00"
    name_off_omdv = len(names)
    names += b"\x00"
    # type list
    tl = bytearray()
    tl += b"OMdi" + struct.pack(">HH", 0, 18)         # refs after numTypes+types
    tl += b"OMdv" + struct.pack(">HH", 0, 30)         # (2 + 2*8 = 18, 30)
    # refs are 12 bytes: id(2) nameOff(2) attrs(1) dataOffset(3) + 4 pad
    def ref(rid, name_off, doff):
        return struct.pack(">HHB", rid, name_off, 0) + doff.to_bytes(3, "big") + b"\x00" * 4
    # resource data sits at fork offset 16 (after the fork header), and
    # dataOffset = 0, so ref offsets are relative to 16
    refs = ref(128, name_off_omdi, 16) + ref(128, name_off_omdv, 16 + len(omdi))
    map_ = (b"\x00" * 16                          # reserved
            + struct.pack(">HHHH", 0, 0, 0, 0)  # copy/next map, file ref, attrs
            + struct.pack(">HH", 28, nlo)       # type list (points AT numTypes) / names
            + struct.pack(">H", 1)              # 2 types - 1
            + bytes(tl)
            + bytes(refs)
            + bytes(names))
    fork = (struct.pack(">IIII", 0, map_off, len(data), len(map_))
            + data
            + b"\x00" * (map_off - 16 - len(data))
            + map_)
    return fork



def _canonical_codes(lengths):
    """Canonical (length, LSB-first code) for the static LZH tables."""
    code = 0
    table = {}
    for length in range(1, 33):
        for sym, ln in enumerate(lengths):
            if ln != length:
                continue
            rev = 0
            for b in range(length):
                rev = (rev << 1) | ((code >> b) & 1)
            table[sym] = (rev, length)
            code += 1
        code <<= 1
    return table


def lzh13_encode(payload):
    """Minimal StuffIt method-13 encoder using static table set 1.

    Emits: selector byte 0x11 (code=1, static set 1), then LSB-first
    codes: literal symbols (<0x100) or match symbols (0x100..0x13d) with
    an offset code + offset bits, then the end symbol 0x140. Only
    literals and length-3 matches with offset<=10 are emitted (the static
    offset table has 11 symbols); sufficient to exercise the decoder."""
    from sit5 import FirstCodeLengths_1, SecondCodeLengths_1, \
        OffsetCodeLengths_1
    first = _canonical_codes(FirstCodeLengths_1)
    second = _canonical_codes(SecondCodeLengths_1)
    offset = _canonical_codes(OffsetCodeLengths_1)

    out = bytearray([0x11])
    bitbuf = 0
    nbits = 0

    def put_code(code, length):
        nonlocal bitbuf, nbits
        bitbuf |= code << nbits
        nbits += length
        while nbits >= 8:
            out.append(bitbuf & 0xFF)
            bitbuf >>= 8
            nbits -= 8

    # Context rule (xad SIT13_Extract / XADStuffIt13Handle): the length
    # symbol is read with the CURRENT table; a literal keeps the first
    # table, a match switches the next token to the second table.
    ctx = first
    i = 0
    while i < len(payload):
        match = None
        if i >= 3:
            for dist in range(1, 11):
                if i - dist >= 0 and payload[i:i + 3] == payload[i - dist:i - dist + 3]:
                    match = dist
                    break
        if match is not None:
            put_code(*ctx[0x100])             # length 3 = sym 0x100 (len = sym-0x100+3)
            ctx = second
            for sym in range(1, 11):
                lo = 2 if sym == 1 else (1 << (sym - 1)) + 1
                hi = 2 if sym == 1 else (1 << sym)
                if lo <= match <= hi:
                    put_code(*offset[sym])
                    if sym > 1:
                        put_code(match - (1 << (sym - 1)) - 1, sym - 1)
                    break
            i += 3
        else:
            put_code(*ctx[payload[i]])
            ctx = first
            i += 1
    put_code(*ctx[0x140])                     # end marker (current ctx)
    if nbits:
        out.append(bitbuf & 0xFF)
    return bytes(out)

def main():
    os.makedirs(OUT, exist_ok=True)
    # 1. hqx fixture: a tiny text file
    payload = (b"USBMIDI9 known-good-driver search: binhex smoke test\r\n"
               + b"RLE run: " + b"AAAA" * 8 + b" and " + b"\x90\x90\x90" + b" end\r\n")
    hqx = hqx_encode("binhex smoke", b"TEXT", b"ttxt", 0, payload, b"")
    open(os.path.join(OUT, "hqx_smoke.hqx"), "wb").write(hqx)

    # 2. omdi fixture: OMdi 128 (MidiSport-style 16 bytes) + OMdv 128 (68K stub)
    omdi = bytes.fromhex("20220000000000000001000000000000")
    omdv = bytes.fromhex("600a00004f4d647600800000" + "4e75" + "00" * 8)
    fork = build_resource_fork(omdi, omdv)
    open(os.path.join(OUT, "omdi_smoke.rsrc"), "wb").write(fork)

    # 3. sit5 fixture: minimal archive container + one method-0 entry whose
    #    resource fork IS the omdi_smoke.rsrc bytes.
    rsrc = fork
    comment = b"StuffIt (c)1997-1998 Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/\r\n"
    entry_name = b"OMS Driver Smoke"
    n = len(entry_name)
    # entry header (version 1): id + fixed fields
    hdr = bytearray()
    hdr += b"\xa5\xa5\xa5\xa5"           # id
    hdr += b"\x01\x00"                    # version, ???
    hdr += struct.pack(">H", 0)           # headersize (patched later)
    hdr += b"\x00\x00"                    # ???, flags
    hdr += struct.pack(">I", 0x01234567)  # creation date
    hdr += struct.pack(">I", 0x01234567)  # mod date
    hdr += struct.pack(">III", 0, 0, 0)   # prev, next, dir
    hdr += struct.pack(">H", n)           # namelen
    hdr += struct.pack(">H", 0)           # header crc (0)
    hdr += struct.pack(">I", 0)           # data size
    hdr += struct.pack(">I", 0)           # data crunched
    hdr += struct.pack(">H", 0)           # data crc
    hdr += b"\x00\x00"                    # ???
    hdr += b"\x00"                        # data algorithm
    hdr += b"\x00"                        # password len
    hdr += entry_name
    hdr[6:8] = struct.pack(">H", len(hdr))  # header size: fixed + name only
    # second block
    hdr += struct.pack(">H", 1)           # resource exists
    hdr += b"\x00\x00"
    hdr += b"OMdv"                        # file type
    hdr += b"MMan"                        # creator
    hdr += struct.pack(">H", 0x2100)      # finder flags
    hdr += b"\xff\xff"
    hdr += b"\x00" * 16
    hdr += b"\x00" * 4
    hdr += struct.pack(">I", len(rsrc))   # rsrc size
    hdr += struct.pack(">I", len(rsrc))   # rsrc crunched (method 0)
    hdr += struct.pack(">H", 0)           # rsrc crc
    hdr += b"\x00\x00"                    # ???
    hdr += b"\x00"                        # rsrc algorithm (0 = none)
    hdr += b"\x00"                        # password len
    entry = bytes(hdr) + rsrc
    # archive header
    ah = bytearray()
    ah += comment
    ah += b"\x1a\x00\x05"                 # magic + version
    ah += b"\x10"                         # flags (14BYTES)
    ah += struct.pack(">I", len(comment) + len(entry) + 21)
    ah += b"\x00\x00\x00\x00"
    ah += struct.pack(">H", 1)            # 1 entry
    # archive header after comment: 1a 00 05 (3) + flags (1) + 16 fixed
    # (tot..crc) + 14-byte extension = 34 bytes
    ah += struct.pack(">I", len(comment) + 34)
    ah += struct.pack(">H", 0)            # crc
    ah += b"\x00" * 14                    # 14-byte extension
    sit = bytes(ah) + entry
    open(os.path.join(OUT, "sit5_smoke.sit"), "wb").write(sit)

    # 4. method-13 fixture: a tiny LZH stream (static set 1) + its plaintext
    plain = b"abcabcabcXYZXYZXYZ"      # has length-3 matches
    lzh = lzh13_encode(plain)
    open(os.path.join(OUT, "lzh13_smoke.bin"), "wb").write(lzh)
    open(os.path.join(OUT, "lzh13_smoke.plain"), "wb").write(plain)

    print("wrote hqx_smoke.hqx (%d B), sit5_smoke.sit (%d B), omdi_smoke.rsrc (%d B), lzh13_smoke.bin (%d B)"
          % (len(hqx), len(sit), len(fork), len(lzh)))


if __name__ == "__main__":
    main()
