#!/usr/bin/env python3
"""PEF v1 packed-data decompressor (corrected op3/op4 semantics).

Usage: pef_unpack.py FILE CONTAINER_OFFSET PACKED_LEN UNPACKED_LEN

Stream format: each byte b: opcode = b >> 5 (top 3 bits), value = b & 0x1F;
if value == 0, value = unpack_next_value() (big-endian 7-bit varint, high
bit = continuation, first byte most significant). Opcodes:
  0 kPEFPkDataZero       emit `value` zero bytes
  1 kPEFPkDataBlock      copy `value` literal bytes
  2 kPEFPkDataRepeat     count = varint; copy the `value`-byte block
                         count+1 times
  3 kPEFPkDataRepeatBlock commonSize = value; customSize = varint;
                         repeatCount = varint; read the common block ONCE,
                         then for each of repeatCount iterations emit the
                         common block then read+emit a FRESH custom block;
                         finally emit the common block once more
  4 kPEFPkDataRepeatZero commonSize = value; customSize = varint;
                         repeatCount = varint; for each of repeatCount
                         iterations emit commonSize ZERO bytes then
                         read+emit a FRESH custom block; finally emit
                         commonSize ZERO bytes
Opcodes 5-7 are reserved. A decode is COMPLETE only when it produces
exactly UNPACKED_LEN bytes AND consumes exactly PACKED_LEN bytes.

CORRECTION HISTORY (promoted from /tmp/pef_unpack.py, 2026-08-17): the
/tmp version carried the PRE-b08bf7c op3/op4 semantics (op3 read count/gap
varints and reused ONE gap block; op4 emitted zeros only). Those made the
authentic TM (167/167) and production (468/468) decodes FAIL with a false
"reserved opcode" error. The semantics above are the verified ones from
pefcheck/src/pef.rs (b08bf7c), which decode both fixtures COMPLETELY —
see docs/re/evidence-ledger.md (packed-data op3/op4 entry) and
docs/re/false-leads.md ("reserved opcode 5/6").
"""
import sys


def unpack_next_value(data, pos):
    """7-bit variable-length integer, BIG-ENDIAN (first byte most
    significant; high bit = continuation) — matches pefcheck/src/pef.rs
    varint() and Ghidra unpackNextValue (`unpacked <<= 7; unpacked +=
    value & 0x7f`). CORRECTED during promotion: the /tmp version
    accumulated little-endian (v |= b<<shift), which diverges on
    multi-byte varints (the fixtures only use single-byte ones, so the
    bug was latent)."""
    v = 0
    while True:
        b = data[pos]
        pos += 1
        v = v * 128 + (b & 0x7F)
        if not (b & 0x80):
            break
    return v, pos


def read_n(data, pos, n):
    if pos + n > len(data):
        raise ValueError("truncated packed stream: need %d at %d (len %d)"
                         % (n, pos, len(data)))
    return data[pos:pos + n], pos + n


def unpack(data, out_len):
    """Return (bytes, consumed) or raise ValueError (overrun/truncation)."""
    out = bytearray()
    pos = 0
    while len(out) < out_len:
        if pos >= len(data):
            raise ValueError("stream exhausted at %d/%d" % (len(out), out_len))
        b = data[pos]
        pos += 1
        opcode = b >> 5
        value = b & 0x1F
        if value == 0:
            value, pos = unpack_next_value(data, pos)
        if opcode == 0:                      # Zero
            out.extend(b"\x00" * value)
        elif opcode == 1:                    # Block
            block, pos = read_n(data, pos, value)
            out.extend(block)
        elif opcode == 2:                    # Repeat
            count, pos = unpack_next_value(data, pos)
            block, pos = read_n(data, pos, value)
            for _ in range(count + 1):
                out.extend(block)
        elif opcode == 3:                    # RepeatBlock (corrected)
            common_size = value
            custom_size, pos = unpack_next_value(data, pos)
            repeat_count, pos = unpack_next_value(data, pos)
            common, pos = read_n(data, pos, common_size)
            for _ in range(repeat_count):
                out.extend(common)
                custom, pos = read_n(data, pos, custom_size)   # FRESH each time
                out.extend(custom)
            out.extend(common)
        elif opcode == 4:                    # RepeatZero (corrected)
            common_size = value
            custom_size, pos = unpack_next_value(data, pos)
            repeat_count, pos = unpack_next_value(data, pos)
            for _ in range(repeat_count):
                out.extend(b"\x00" * common_size)
                custom, pos = read_n(data, pos, custom_size)   # FRESH each time
                out.extend(custom)
            out.extend(b"\x00" * common_size)
        else:
            raise ValueError("reserved opcode %d at %d" % (opcode, pos - 1))
    if len(out) > out_len:
        raise ValueError("overrun: %d > %d" % (len(out), out_len))
    if pos != len(data):
        raise ValueError("stream consumed %d of %d packed bytes (not COMPLETE)"
                         % (pos, len(data)))
    return bytes(out), pos


def main():
    path, off, size, out_len = (sys.argv[1], int(sys.argv[2], 0),
                                int(sys.argv[3], 0), int(sys.argv[4], 0))
    data = open(path, "rb").read()
    packed = data[off:off + size]
    out, consumed = unpack(packed, out_len)
    print("COMPLETE: packed %d -> unpacked %d (expected %d)"
          % (consumed, len(out), out_len))
    for i in range(0, len(out), 16):
        chunk = out[i:i + 16]
        print("%04x: %s" % (i, chunk.hex(" ")))


if __name__ == "__main__":
    main()
