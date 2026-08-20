#!/usr/bin/env python3
"""Decode a BinHex 4.0 / HQX file into (data fork) + (resource fork).

Usage: binhex_decode.py INPUT.hqx OUTPREFIX
Writes OUTPREFIX.data and OUTPREFIX.rsrc (rsrc may be empty).

Alphabet and RLE semantics follow macutils hexbin/hqx.c (the canonical
reference decoder). BinHex 4.0 packs 4 six-bit chars into 3 bytes (MSB
first) and applies run-length encoding on the unpacked byte stream with
RUNCHAR = 0x90: [0x90, 0x00] is a literal 0x90; [0x90, N>0] repeats the
previous byte N times.

Header (Peter N Lewis "BinHex 4.0 Definition", Aug 1991):
  [nameLen][name][version 0x00][type 4][creator 4][flags 2]
  [dataLen 4][rsrcLen 4][CRC 2]            (crc at 20+n)
CRCs are CRC-16/CCITT (poly 0x1021, init 0) over the decoded stream up
to (but excluding) the CRC field. Trailing bytes after the final CRC are
tolerated (StuffIt writers append excess data). The +1-byte variant is
accepted as a tolerance fallback but has never been observed.
"""
import binascii
import sys

# BinHex 4.0 alphabet (64 chars): value -> char.
# !"#$%&'()*+,- 012345689 @ A-N P-V X-[ ` a-f h-m p-r
# ('g','7','O','W','n','s'-'z' are NOT in the alphabet; 'r' IS, value 0x3F.)
ALPHA = ("!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`"
         "abcdefhijklmpqr")
CHAR_TO_VAL = {c: v for v, c in enumerate(ALPHA)}
RUNCHAR = 0x90


def decode_hqx(data):
    """Return the fully decoded byte stream (RLE-expanded)."""
    text = data.decode("latin1")
    out = []
    started = False
    for ch in text:
        if not started:
            if ch == ":":
                started = True
            continue
        if ch == ":":
            break
        if ch in "\r\n \t":
            continue
        v = CHAR_TO_VAL.get(ch)
        if v is None:
            raise ValueError("bad binhex char %r" % ch)
        out.append(v)
    # 6-bit -> 8-bit (MSB first, 4 chars -> 3 bytes)
    bits = 0
    nbits = 0
    packed = bytearray()
    for v in out:
        bits = (bits << 6) | v
        nbits += 6
        while nbits >= 8:
            nbits -= 8
            packed.append((bits >> nbits) & 0xFF)
        bits &= (1 << nbits) - 1  # drop consumed high bits (keeps bits < 2^12)
    # Run-length decode: [X][0x90][N] = X repeated N times TOTAL (the count
    # includes the byte already output); [X][0x90][0] = literal 0x90.
    # (Peter N Lewis "BinHex 4.0 Definition": "FF9004 means repeat 0xFF 4
    # times"; the run repeats the byte BEFORE the 0x90, or the literal 0x90
    # itself for 2B90009005 -> 2B 90 90 90 90 90.)
    raw = bytearray()
    run = False
    lastc = 0
    for b in packed:
        if not run:
            if b == RUNCHAR:
                run = True
            else:
                raw.append(b)
                lastc = b
        else:
            if b == 0:
                raw.append(RUNCHAR)
                lastc = RUNCHAR
            else:
                for _ in range(b - 1):
                    raw.append(lastc)
            run = False
    return bytes(raw)


def parse_header(raw):
    """Try both header layouts; return dict or None."""
    n = raw[0]
    if len(raw) < 22 + n + 2:
        return None
    for extra in (0, 1):
        hdr_end = 20 + n + extra
        calc = binascii.crc_hqx(raw[0:hdr_end], 0)
        stored = int.from_bytes(raw[hdr_end:hdr_end + 2], "big")
        if calc == stored:
            return {
                "n": n,
                "name": raw[1:1 + n].decode("latin1"),
                "type": raw[2 + n:6 + n].decode("latin1"),
                "creator": raw[6 + n:10 + n].decode("latin1"),
                "flags": int.from_bytes(raw[10 + n:12 + n], "big"),
                "dlen": int.from_bytes(raw[12 + n:16 + n], "big"),
                "rlen": int.from_bytes(raw[16 + n:20 + n], "big"),
                "crc_off": hdr_end,
            }
    return None


def main():
    fn, prefix = sys.argv[1], sys.argv[2]
    with open(fn, "rb") as f:
        raw = decode_hqx(f.read())
    h = parse_header(raw)
    if h is None:
        sys.exit("header CRC mismatch (neither layout A nor B validates)")
    pos = h["crc_off"] + 2
    data = raw[pos:pos + h["dlen"]]
    pos += h["dlen"]
    if binascii.crc_hqx(raw[0:pos], 0) != int.from_bytes(raw[pos:pos + 2], "big"):
        sys.exit("data CRC mismatch")
    pos += 2
    rsrc = raw[pos:pos + h["rlen"]]
    pos += h["rlen"]
    if binascii.crc_hqx(raw[0:pos], 0) != int.from_bytes(raw[pos:pos + 2], "big"):
        sys.exit("rsrc CRC mismatch")
    with open(prefix + ".data", "wb") as f:
        f.write(data)
    with open(prefix + ".rsrc", "wb") as f:
        f.write(rsrc)
    print("name=%r type=%r creator=%r data=%d rsrc=%d (trailing=%d)"
          % (h["name"], h["type"], h["creator"], len(data), len(rsrc),
             len(raw) - pos - 2))


if __name__ == "__main__":
    main()
