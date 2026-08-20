#!/usr/bin/env python3
"""StuffIt 5 archive inspection and method-13 ("fastest") decompression.

Usage:
  sit5.py LIST ARCHIVE          list the archive entries (SIT5 format)
  sit5.py EXTRACT ARCHIVE DIR   extract every entry payload (.comp blobs)
  sit5.py DECOMPRESS METHOD IN OUTLEN OUT   decompress one method-13 fork

The LZH decompressor (method 13 = "fastest") is a clean-room port of the
algorithm in the Amiga xad library (Dirk Stoecker, GPL) and MacPaw's
XADMaster XADStuffIt13Handle (LGPL 2.1), validated end-to-end on authentic
vendor OMS driver archives (2026-08-21): every decoded fork's IBM CRC16
(poly 0x8005 reflected, init 0) matches the CRC stored in the SIT5 entry
header. Method 15 ("max") of StuffIt 5.0/5.1-era archives is NOT the "As"
signature Arsenic format and is not implemented here.

Bits are read LSB-first within bytes. Stream layout:
  byte 0: high nibble = code selector (0 = dynamic tables via the 37-symbol
          metacode; 1..5 = static table set)
  LZSS symbols: <0x100 literal; 0x100..0x13d match len = sym-0x100+3;
  0x13e len = bits10+65; 0x13f len = bits15+65; >=0x140 end
  match offset: offcode symbol -> bl; 0 -> 1, 1 -> 2,
  else (1<<(bl-1)) + bits(bl-1) + 1
"""
import struct
import sys

# Static Huffman code-length tables for the StuffIt LZH (method 13)
# decompressor, extracted from XADMaster XADStuffIt13Handle.m
# (LGPL 2.1, MacPaw) during the 2026-08-21 known-good-driver search.
# 321 (first/second) or 11-14 (offset) entries per table; canonical
# codes are assigned by length then symbol index.
FirstCodeLengths_1 = [
    4, 5, 7, 8, 8, 9, 9, 9, 9, 7, 9, 9, 9, 8, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 10, 9, 9, 10, 10, 9, 10, 9, 9, 5, 9, 9, 9, 9, 10, 9, 9,
    9, 9, 9, 9, 9, 9, 7, 9, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 8, 9, 9, 8, 8, 9, 9, 9, 9, 9, 9, 9, 7, 8, 9,
    7, 9, 9, 7, 7, 9, 9, 9, 9, 10, 9, 10, 10, 10, 9, 9, 9, 5, 9, 8,
    7, 5, 9, 8, 8, 7, 9, 9, 8, 8, 5, 5, 7, 10, 5, 8, 5, 8, 9, 9,
    9, 9, 9, 10, 9, 9, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10,
    10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10,
    9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 9, 5, 6, 5, 5, 8,
    9, 9, 9, 9, 9, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 9, 9, 9, 10, 9, 10, 9,
    10, 9, 10, 9, 10, 10, 10, 9, 10, 9, 10, 10, 9, 9, 9, 6, 9, 9, 10, 9,
    5,
]
FirstCodeLengths_2 = [
    4, 7, 7, 8, 7, 8, 8, 8, 8, 7, 8, 7, 8, 7, 9, 8, 8, 8, 9, 9,
    9, 9, 10, 10, 9, 10, 10, 10, 10, 10, 9, 9, 5, 9, 8, 9, 9, 11, 10, 9,
    8, 9, 9, 9, 8, 9, 7, 8, 8, 8, 9, 9, 9, 9, 9, 10, 9, 9, 9, 10,
    9, 9, 10, 9, 8, 8, 7, 7, 7, 8, 8, 9, 8, 8, 9, 9, 8, 8, 7, 8,
    7, 10, 8, 7, 7, 9, 9, 9, 9, 10, 10, 11, 11, 11, 10, 9, 8, 6, 8, 7,
    7, 5, 7, 7, 7, 6, 9, 8, 6, 7, 6, 6, 7, 9, 6, 6, 6, 7, 8, 8,
    8, 8, 9, 10, 9, 10, 9, 9, 8, 9, 10, 10, 9, 10, 10, 9, 9, 10, 10, 10,
    10, 10, 10, 10, 9, 10, 10, 11, 10, 10, 10, 10, 10, 10, 10, 11, 10, 11, 10, 10,
    9, 11, 10, 10, 10, 10, 10, 10, 9, 9, 10, 11, 10, 11, 10, 11, 10, 12, 10, 11,
    10, 12, 11, 12, 10, 12, 10, 11, 10, 11, 11, 11, 9, 10, 11, 11, 11, 12, 12, 10,
    10, 10, 11, 11, 10, 11, 10, 10, 9, 11, 10, 11, 10, 11, 11, 11, 10, 11, 11, 12,
    11, 11, 10, 10, 10, 11, 10, 10, 11, 11, 12, 10, 10, 11, 11, 12, 11, 11, 10, 11,
    9, 12, 10, 11, 11, 11, 10, 11, 10, 11, 10, 11, 9, 10, 9, 7, 3, 5, 6, 6,
    7, 7, 8, 8, 8, 9, 9, 9, 11, 10, 10, 10, 12, 13, 11, 12, 12, 11, 13, 12,
    12, 11, 12, 12, 13, 12, 14, 13, 14, 13, 15, 13, 14, 15, 15, 14, 13, 15, 15, 14,
    15, 14, 15, 15, 14, 15, 13, 13, 14, 15, 15, 14, 14, 16, 16, 15, 15, 15, 12, 15,
    10,
]
FirstCodeLengths_3 = [
    6, 6, 6, 6, 6, 9, 8, 8, 4, 9, 8, 9, 8, 9, 9, 9, 8, 9, 9, 10,
    8, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 9, 9, 9, 8, 10, 9, 10, 9, 10,
    9, 10, 9, 10, 9, 9, 8, 9, 8, 9, 9, 9, 10, 10, 10, 10, 9, 9, 9, 10,
    9, 10, 9, 9, 7, 8, 8, 9, 8, 9, 9, 9, 8, 9, 9, 10, 9, 9, 8, 9,
    8, 9, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 9, 8, 8, 9, 8,
    9, 7, 8, 8, 9, 8, 10, 10, 8, 9, 8, 8, 8, 10, 8, 8, 8, 8, 9, 9,
    9, 9, 10, 10, 10, 10, 10, 9, 7, 9, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10,
    10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10,
    9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 9, 8, 9, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 9,
    9, 10, 10, 10, 10, 10, 10, 9, 9, 10, 9, 9, 8, 9, 8, 9, 4, 6, 6, 6,
    7, 8, 8, 9, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 7, 10, 10, 10, 7, 10, 10, 7, 7, 7, 7, 7, 6, 7,
    10, 7, 7, 10, 7, 7, 7, 6, 7, 6, 6, 7, 7, 6, 6, 9, 6, 9, 10, 6,
    10,
]
FirstCodeLengths_4 = [
    2, 6, 6, 7, 7, 8, 7, 8, 7, 8, 8, 9, 8, 9, 9, 9, 8, 8, 9, 9,
    9, 10, 10, 9, 8, 10, 9, 10, 9, 10, 9, 9, 6, 9, 8, 9, 9, 10, 9, 9,
    9, 10, 9, 9, 9, 9, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 10, 10, 9, 7, 7, 8, 8, 8, 8, 9, 9, 7, 8, 9, 10, 8, 8, 7, 8,
    8, 10, 8, 8, 8, 9, 8, 9, 9, 10, 9, 11, 10, 11, 9, 9, 8, 7, 9, 8,
    8, 6, 8, 8, 8, 7, 10, 9, 7, 8, 7, 7, 8, 10, 7, 7, 7, 8, 9, 9,
    9, 9, 10, 11, 9, 11, 10, 9, 7, 9, 10, 10, 10, 11, 11, 10, 10, 11, 10, 10,
    10, 11, 11, 10, 9, 10, 10, 11, 10, 11, 10, 11, 10, 10, 10, 11, 10, 11, 10, 10,
    9, 10, 10, 11, 10, 10, 10, 10, 9, 10, 10, 10, 10, 11, 10, 11, 10, 11, 10, 11,
    11, 11, 10, 12, 10, 11, 10, 11, 10, 11, 11, 10, 8, 10, 10, 11, 10, 11, 11, 11,
    10, 11, 10, 11, 10, 11, 11, 11, 9, 10, 11, 11, 10, 11, 11, 11, 10, 11, 11, 11,
    10, 10, 10, 10, 10, 11, 10, 10, 11, 11, 10, 10, 9, 11, 10, 10, 11, 11, 10, 10,
    10, 11, 10, 10, 10, 10, 10, 10, 9, 11, 10, 10, 8, 10, 8, 6, 5, 6, 6, 7,
    7, 8, 8, 8, 9, 10, 11, 10, 10, 11, 11, 12, 12, 10, 11, 12, 12, 12, 12, 13,
    13, 13, 13, 13, 12, 13, 13, 15, 14, 12, 14, 15, 16, 12, 12, 13, 15, 14, 16, 15,
    17, 18, 15, 17, 16, 15, 15, 15, 15, 13, 13, 10, 14, 12, 13, 17, 17, 18, 10, 17,
    4,
]
FirstCodeLengths_5 = [
    7, 9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 9, 7, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 9, 5, 9, 7, 9, 9, 9, 9, 9,
    7, 7, 7, 9, 7, 7, 8, 7, 8, 8, 7, 7, 9, 9, 9, 9, 7, 7, 7, 9,
    9, 9, 9, 9, 9, 7, 9, 7, 7, 7, 7, 9, 9, 7, 9, 9, 7, 7, 7, 7,
    7, 9, 7, 8, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 7, 8, 7,
    7, 7, 8, 8, 6, 7, 9, 7, 7, 8, 7, 5, 6, 9, 5, 7, 5, 6, 7, 7,
    9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 10, 10, 10, 9, 9, 10, 10, 10,
    10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10,
    9, 10, 10, 10, 9, 9, 10, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10,
    9, 9, 9, 10, 10, 10, 10, 10, 9, 10, 9, 10, 10, 9, 10, 10, 9, 10, 10, 10,
    10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 9, 10, 9, 10, 10, 9, 5, 6, 8, 8,
    7, 7, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 5, 10, 8, 9, 8,
    9,
]
SecondCodeLengths_1 = [
    4, 5, 6, 6, 7, 7, 6, 7, 7, 7, 6, 8, 7, 8, 8, 8, 8, 9, 6, 9,
    8, 9, 8, 9, 9, 9, 8, 10, 5, 9, 7, 9, 6, 9, 8, 10, 9, 10, 8, 8,
    9, 9, 7, 9, 8, 9, 8, 9, 8, 8, 6, 9, 9, 8, 8, 9, 9, 10, 8, 9,
    9, 10, 8, 10, 8, 8, 8, 8, 8, 9, 7, 10, 6, 9, 9, 11, 7, 8, 8, 9,
    8, 10, 7, 8, 6, 9, 10, 9, 9, 10, 8, 11, 9, 11, 9, 10, 9, 8, 9, 8,
    8, 8, 8, 10, 9, 9, 10, 10, 8, 9, 8, 8, 8, 11, 9, 8, 8, 9, 9, 10,
    8, 11, 10, 10, 8, 10, 9, 10, 8, 9, 9, 11, 9, 11, 9, 10, 10, 11, 10, 12,
    9, 12, 10, 11, 10, 11, 9, 10, 10, 11, 10, 11, 10, 11, 10, 11, 10, 10, 10, 9,
    9, 9, 8, 7, 6, 8, 11, 11, 9, 12, 10, 12, 9, 11, 11, 11, 10, 12, 11, 11,
    10, 12, 10, 11, 10, 10, 10, 11, 10, 11, 11, 11, 9, 12, 10, 12, 11, 12, 10, 11,
    10, 12, 11, 12, 11, 12, 11, 12, 10, 12, 11, 12, 11, 11, 10, 12, 10, 11, 10, 12,
    10, 12, 10, 12, 10, 11, 11, 11, 10, 11, 11, 11, 10, 12, 11, 12, 10, 10, 11, 11,
    9, 12, 11, 12, 10, 11, 10, 12, 10, 11, 10, 12, 10, 11, 10, 7, 5, 4, 6, 6,
    7, 7, 7, 8, 8, 7, 7, 6, 8, 6, 7, 7, 9, 8, 9, 9, 10, 11, 11, 11,
    12, 11, 10, 11, 12, 11, 12, 11, 12, 12, 12, 12, 11, 12, 12, 11, 12, 11, 12, 11,
    13, 11, 12, 10, 13, 10, 14, 14, 13, 14, 15, 14, 16, 15, 15, 18, 18, 18, 9, 18,
    8,
]
SecondCodeLengths_2 = [
    5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 8, 7, 8, 7, 7, 7, 8, 8, 8,
    8, 9, 8, 9, 8, 9, 9, 9, 7, 9, 8, 8, 6, 9, 8, 9, 8, 9, 8, 9,
    8, 9, 8, 9, 8, 9, 8, 8, 8, 8, 8, 9, 8, 9, 8, 9, 9, 10, 8, 10,
    8, 9, 9, 8, 8, 8, 7, 8, 8, 9, 8, 9, 7, 9, 8, 10, 8, 9, 8, 9,
    8, 9, 8, 8, 8, 9, 9, 9, 9, 10, 9, 11, 9, 10, 9, 10, 8, 8, 8, 9,
    8, 8, 8, 9, 9, 8, 9, 10, 8, 9, 8, 8, 8, 11, 8, 7, 8, 9, 9, 9,
    9, 10, 9, 10, 9, 10, 9, 8, 8, 9, 9, 10, 9, 10, 9, 10, 8, 10, 9, 10,
    9, 11, 10, 11, 9, 11, 10, 10, 10, 11, 9, 11, 9, 10, 9, 11, 9, 11, 10, 10,
    9, 10, 9, 9, 8, 10, 9, 11, 9, 9, 9, 11, 10, 11, 9, 11, 9, 11, 9, 11,
    10, 11, 10, 11, 10, 11, 9, 10, 10, 11, 10, 10, 8, 10, 9, 10, 10, 11, 9, 11,
    9, 10, 10, 11, 9, 10, 10, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 11, 9, 11,
    10, 10, 9, 10, 9, 11, 9, 11, 9, 11, 9, 10, 9, 11, 9, 11, 9, 11, 9, 10,
    8, 11, 9, 10, 9, 10, 9, 10, 8, 10, 8, 9, 8, 9, 8, 7, 4, 4, 5, 6,
    6, 6, 7, 7, 7, 7, 8, 8, 8, 7, 8, 8, 9, 9, 10, 10, 10, 10, 10, 10,
    11, 11, 10, 10, 12, 11, 11, 12, 12, 11, 12, 12, 11, 12, 12, 12, 12, 12, 12, 11,
    12, 11, 13, 12, 13, 12, 13, 14, 14, 14, 15, 13, 14, 13, 14, 18, 18, 17, 7, 16,
    9,
]
SecondCodeLengths_3 = [
    5, 6, 6, 6, 6, 7, 7, 7, 6, 8, 7, 8, 7, 9, 8, 8, 7, 7, 8, 9,
    9, 9, 9, 10, 8, 9, 9, 10, 8, 10, 9, 8, 6, 10, 8, 10, 8, 10, 9, 9,
    9, 9, 9, 10, 9, 9, 8, 9, 8, 9, 8, 9, 9, 10, 9, 10, 9, 9, 8, 10,
    9, 11, 10, 8, 8, 8, 8, 9, 7, 9, 9, 10, 8, 9, 8, 11, 9, 10, 9, 10,
    8, 9, 9, 9, 9, 8, 9, 9, 10, 10, 10, 12, 10, 11, 10, 10, 8, 9, 9, 9,
    8, 9, 8, 8, 10, 9, 10, 11, 8, 10, 9, 9, 8, 12, 8, 9, 9, 9, 9, 8,
    9, 10, 9, 12, 10, 10, 10, 8, 7, 11, 10, 9, 10, 11, 9, 11, 7, 11, 10, 12,
    10, 12, 10, 11, 9, 11, 9, 12, 10, 12, 10, 12, 10, 9, 11, 12, 10, 12, 10, 11,
    9, 10, 9, 10, 9, 11, 11, 12, 9, 10, 8, 12, 11, 12, 9, 12, 10, 12, 10, 13,
    10, 12, 10, 12, 10, 12, 10, 9, 10, 12, 10, 9, 8, 11, 10, 12, 10, 12, 10, 12,
    10, 11, 10, 12, 8, 12, 10, 11, 10, 10, 10, 12, 9, 11, 10, 12, 10, 12, 11, 12,
    10, 9, 10, 12, 9, 10, 10, 12, 10, 11, 10, 11, 10, 12, 8, 12, 9, 12, 8, 12,
    8, 11, 10, 11, 10, 11, 9, 10, 8, 10, 9, 9, 8, 9, 8, 7, 4, 3, 5, 5,
    6, 5, 6, 6, 7, 7, 8, 8, 8, 7, 7, 7, 9, 8, 9, 9, 11, 9, 11, 9,
    8, 9, 9, 11, 12, 11, 12, 12, 13, 13, 12, 13, 14, 13, 14, 13, 14, 13, 13, 13,
    12, 13, 13, 12, 13, 13, 14, 14, 13, 13, 14, 14, 14, 14, 15, 18, 17, 18, 8, 16,
    10,
]
SecondCodeLengths_4 = [
    4, 5, 6, 6, 6, 6, 7, 7, 6, 7, 7, 9, 6, 8, 8, 7, 7, 8, 8, 8,
    6, 9, 8, 8, 7, 9, 8, 9, 8, 9, 8, 9, 6, 9, 8, 9, 8, 10, 9, 9,
    8, 10, 8, 10, 8, 9, 8, 9, 8, 8, 7, 9, 9, 9, 9, 9, 8, 10, 9, 10,
    9, 10, 9, 8, 7, 8, 9, 9, 8, 9, 9, 9, 7, 10, 9, 10, 9, 9, 8, 9,
    8, 9, 8, 8, 8, 9, 9, 10, 9, 9, 8, 11, 9, 11, 10, 10, 8, 8, 10, 8,
    8, 9, 9, 9, 10, 9, 10, 11, 9, 9, 9, 9, 8, 9, 8, 8, 8, 10, 10, 9,
    9, 8, 10, 11, 10, 11, 11, 9, 8, 9, 10, 11, 9, 10, 11, 11, 9, 12, 10, 10,
    10, 12, 11, 11, 9, 11, 11, 12, 9, 11, 9, 10, 10, 10, 10, 12, 9, 11, 10, 11,
    9, 11, 11, 11, 10, 11, 11, 12, 9, 10, 10, 12, 11, 11, 10, 11, 9, 11, 10, 11,
    10, 11, 9, 11, 11, 9, 8, 11, 10, 11, 11, 10, 7, 12, 11, 11, 11, 11, 11, 12,
    10, 12, 11, 13, 11, 10, 12, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 11, 11,
    10, 11, 10, 10, 10, 11, 10, 12, 11, 12, 10, 11, 9, 11, 10, 11, 10, 11, 10, 12,
    9, 11, 11, 11, 9, 11, 10, 10, 9, 11, 10, 10, 9, 10, 9, 7, 4, 5, 5, 5,
    6, 6, 7, 6, 8, 7, 8, 9, 9, 7, 8, 8, 10, 9, 10, 10, 12, 10, 11, 11,
    11, 11, 10, 11, 12, 11, 11, 11, 11, 11, 13, 12, 11, 12, 13, 12, 12, 12, 13, 11,
    9, 12, 13, 7, 13, 11, 13, 11, 10, 11, 13, 15, 15, 12, 14, 15, 15, 15, 6, 15,
    5,
]
SecondCodeLengths_5 = [
    8, 10, 11, 11, 11, 12, 11, 11, 12, 6, 11, 12, 10, 5, 12, 12, 12, 12, 12, 12,
    12, 13, 13, 14, 13, 13, 12, 13, 12, 13, 12, 15, 4, 10, 7, 9, 11, 11, 10, 9,
    6, 7, 8, 9, 6, 7, 6, 7, 8, 7, 7, 8, 8, 8, 8, 8, 8, 9, 8, 7,
    10, 9, 10, 10, 11, 7, 8, 6, 7, 8, 8, 9, 8, 7, 10, 10, 8, 7, 8, 8,
    7, 10, 7, 6, 7, 9, 9, 8, 11, 11, 11, 10, 11, 11, 11, 8, 11, 6, 7, 6,
    6, 6, 6, 8, 7, 6, 10, 9, 6, 7, 6, 6, 7, 10, 6, 5, 6, 7, 7, 7,
    10, 8, 11, 9, 13, 7, 14, 16, 12, 14, 14, 15, 15, 16, 16, 14, 15, 15, 15, 15,
    15, 15, 15, 15, 14, 15, 13, 14, 14, 16, 15, 17, 14, 17, 15, 17, 12, 14, 13, 16,
    12, 17, 13, 17, 14, 13, 13, 14, 14, 12, 13, 15, 15, 14, 15, 17, 14, 17, 15, 14,
    15, 16, 12, 16, 15, 14, 15, 16, 15, 16, 17, 17, 15, 15, 17, 17, 13, 14, 15, 15,
    13, 12, 16, 16, 17, 14, 15, 16, 15, 15, 13, 13, 15, 13, 16, 17, 15, 17, 17, 17,
    16, 17, 14, 17, 14, 16, 15, 17, 15, 15, 14, 17, 15, 17, 15, 16, 15, 15, 16, 16,
    14, 17, 17, 15, 15, 16, 15, 17, 15, 14, 16, 16, 16, 16, 16, 12, 4, 4, 5, 5,
    6, 6, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 11, 10,
    11, 11, 11, 11, 11, 12, 12, 12, 13, 13, 12, 13, 12, 14, 14, 12, 13, 13, 13, 13,
    14, 12, 13, 13, 14, 14, 14, 13, 14, 14, 15, 15, 13, 15, 13, 17, 17, 17, 9, 17,
    7,
]
OffsetCodeLengths_1 = [
    5, 6, 3, 3, 3, 3, 3, 3, 3, 4, 6,
]
OffsetCodeLengths_2 = [
    5, 6, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 6,
]
OffsetCodeLengths_3 = [
    6, 7, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 5, 7,
]
OffsetCodeLengths_4 = [
    3, 6, 5, 4, 2, 3, 3, 3, 4, 4, 6,
]
OffsetCodeLengths_5 = [
    6, 7, 7, 6, 4, 3, 2, 2, 3, 3, 6,
]

# The tables above end the static data; the decoder follows. The SIT5
# method-13 stream is the classic StuffIt LZH: byte 0 high nibble = code
# selector (0 = dynamic via the 37-symbol metacode, 1..5 = static set);
# LZSS symbols <0x100 literal; 0x100..0x13d match len = sym-0x100+3;
# 0x13e/0x13f len = bits(10/15)+65; >=0x140 end; offset: offcode -> bl,
# 0->1, 1->2, else (1<<(bl-1)) + bits(bl-1) + 1. Bits are LSB-first.

# ---------------------------------------------------------------------------
# bit reader (LSB-first)


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.bit = 0

    def bit_remaining(self):
        return (len(self.data) - self.pos) * 8 - self.bit

    def next_bit(self):
        if self.pos >= len(self.data):
            raise EOFError("bit stream exhausted")
        b = (self.data[self.pos] >> self.bit) & 1
        self.bit += 1
        if self.bit == 8:
            self.bit = 0
            self.pos += 1
        return b

    def next_bits(self, n):
        v = 0
        for i in range(n):
            v |= self.next_bit() << i
        return v


# ---------------------------------------------------------------------------
# prefix code


def build_canonical(lengths, numsymbols, zeros=True, maxlength=32):
    """Canonical codes (XADPrefixCode initWithLengths:... shortestCodeIsZeros:).
    Returns dict: reversed(L-bit) code -> (length, symbol)."""
    code = 0
    table = {}
    symbolsleft = numsymbols
    for length in range(1, maxlength + 1):
        for i in range(numsymbols):
            if lengths[i] != length:
                continue
            c = code if zeros else (~code) & ((1 << length) - 1)
            # reverse within L bits for LSB-first decoding
            rev = 0
            for b in range(length):
                rev = (rev << 1) | ((c >> b) & 1)
            table[rev] = (length, i)
            code += 1
            symbolsleft -= 1
            if symbolsleft == 0:
                return table
        code <<= 1
    return table


def build_direct(codes, lengths, numsymbols):
    """Direct-insertion code (metacode): codes given LSB-first."""
    table = {}
    for i in range(numsymbols):
        table[codes[i]] = (lengths[i], i)
    return table


class PrefixCode:
    def __init__(self, table):
        self.table = table  # dict revcode -> (length, symbol)

    def next_symbol(self, br):
        v = 0
        for n in range(1, 33):
            v |= br.next_bit() << (n - 1)
            e = self.table.get(v)
            if e is not None and e[0] == n:
                return e[1]
        raise ValueError("unterminated code")


# ---------------------------------------------------------------------------
# metacode (symbols 31..36 are special in the dynamic table parse)
META_CODES = [
    0x5d8, 0x058, 0x040, 0x0c0, 0x000, 0x078, 0x02b, 0x014,
    0x00c, 0x01c, 0x01b, 0x00b, 0x010, 0x020, 0x038, 0x018,
    0x0d8, 0xbd8, 0x180, 0x680, 0x380, 0xf80, 0x780, 0x480,
    0x080, 0x280, 0x3d8, 0xfd8, 0x7d8, 0x9d8, 0x1d8, 0x004,
    0x001, 0x002, 0x007, 0x003, 0x008,
]
META_LENGTHS = [
    11, 8, 8, 8, 8, 7, 6, 5, 5, 5, 5, 6, 5, 6, 7, 7, 9, 12, 10, 11, 11, 12,
    12, 11, 11, 11, 12, 12, 12, 12, 12, 5, 2, 2, 3, 4, 5,
]
META_CODE = build_direct(META_CODES, META_LENGTHS, 37)
_METACODE_OBJ = PrefixCode(META_CODE)


def parse_dynamic_table(br, metacode, numcodes):
    """allocAndParseCodeOfSize: — exact C semantics incl. the post-switch
    lengths[i]=length write (case 34 bit=1 / 35 / 36 also advance i inside
    the switch, so those writes land in the following slot(s))."""
    length = 0
    lengths = []
    i = 0
    while i < numcodes:
        val = metacode.next_symbol(br)
        if val == 31:
            length = -1
        elif val == 32:
            length += 1
        elif val == 33:
            length -= 1
        elif val == 34:
            if br.next_bit():
                lengths.append(length)
                i += 1
        elif val == 35:
            cnt = br.next_bits(3) + 2
            for _ in range(cnt):
                lengths.append(length)
                i += 1
        elif val == 36:
            cnt = br.next_bits(6) + 10
            for _ in range(cnt):
                lengths.append(length)
                i += 1
        else:
            length = val + 1
        if i < numcodes:
            lengths.append(length)
            i += 1
    return build_canonical(lengths, numcodes, zeros=True, maxlength=32)


# ---------------------------------------------------------------------------
# static tables (parsed from XADStuffIt13Handle.m by the import script)
def make_static_code(lengths):
    return PrefixCode(build_canonical(lengths, len(lengths), zeros=True, maxlength=32))


STATIC = {}
for _n in range(1, 6):
    STATIC["firstset%d" % _n] = eval("FirstCodeLengths_%d" % _n)
    STATIC["secondset%d" % _n] = eval("SecondCodeLengths_%d" % _n)
    STATIC["offsetset%d" % _n] = eval("OffsetCodeLengths_%d" % _n)


# ---------------------------------------------------------------------------
# the decompressor


def decompress(data, out_len):
    br = BitReader(data)
    val = br.next_bits(8)
    code = val >> 4
    if code == 0:
        first = PrefixCode(parse_dynamic_table(br, _METACODE_OBJ, 321))
        if val & 0x08:
            second = first
        else:
            second = PrefixCode(parse_dynamic_table(br, _METACODE_OBJ, 321))
        offset = PrefixCode(parse_dynamic_table(br, _METACODE_OBJ, (val & 0x07) + 10))
    elif code < 6:
        key = "set%d" % code
        first = make_static_code(STATIC["first" + key])
        second = make_static_code(STATIC["second" + key])
        offset = make_static_code(STATIC["offset" + key])
    else:
        raise ValueError("bad code selector %d" % code)

    currcode = first
    out = bytearray()
    pos = 0
    while len(out) < out_len:
        sym = currcode.next_symbol(br)
        if sym < 0x100:
            currcode = first
            out.append(sym)
        else:
            currcode = second
            if sym < 0x13e:
                length = sym - 0x100 + 3
            elif sym == 0x13e:
                length = br.next_bits(10) + 65
            elif sym == 0x13f:
                length = br.next_bits(15) + 65
            else:
                break  # end marker
            bl = offset.next_symbol(br)
            if bl == 0:
                dist = 1
            elif bl == 1:
                dist = 2
            else:
                dist = (1 << (bl - 1)) + br.next_bits(bl - 1) + 1
            for _ in range(length):
                b = out[len(out) - dist]
                out.append(b)
    return bytes(out[:out_len])


def list_archive(path):
    """Print the SIT5 entry tree of an archive file (or .data fork)."""
    data = open(path, "rb").read()
    i = data.find(b"\x1a\x00\x05")
    if i < 0:
        sys.exit("no SIT5 magic in %s" % path)
    base = i + 2
    nf = struct.unpack(">H", data[base + 10:base + 12])[0]
    fo = struct.unpack(">I", data[base + 12:base + 16])[0]
    print("archive %s: %d top-level entries, first at %d" % (path, nf, fo))
    walk(data, fo, None, 0, nf + 2)


def walk(data, off, outprefix, depth, budget):
    ind = "  " * depth
    for _ in range(budget):
        p = off
        if p + 8 > len(data):
            return
        if data[p:p + 4] != b"\xa5\xa5\xa5\xa5":
            return
        if p + 12 > len(data):
            return
        version = data[p + 4]
        headersize = struct.unpack(">H", data[p + 6:p + 8])[0]
        headerend = off + headersize
        if headerend > len(data):
            return
        flags = data[p + 9]
        q = p + 10 + 20
        namelength = struct.unpack(">H", data[q:q + 2])[0]
        q += 4
        datalength = struct.unpack(">I", data[q:q + 4])[0]
        datacomplen = struct.unpack(">I", data[q + 4:q + 8])[0]
        q += 12
        isdir = flags & 0x40
        if isdir:
            numfiles = struct.unpack(">H", data[q:q + 2])[0]
            q += 2
        else:
            q += 2
            q += data[q - 1]
        name = data[q:q + namelength]
        q += namelength
        if q < headerend and q + 2 <= len(data):
            q += 4 + struct.unpack(">H", data[q:q + 2])[0]
        if q + 2 > len(data):
            return
        something = struct.unpack(">H", data[q:q + 2])[0]
        q += 4
        q += 10  # type + creator + finder flags
        q += 22 if version == 1 else 18
        rlen = rcomplen = rcrc = rmethod = 0
        if something & 1:
            rlen = struct.unpack(">I", data[q:q + 4])[0]
            rcomplen = struct.unpack(">I", data[q + 4:q + 8])[0]
            rcrc = struct.unpack(">H", data[q + 8:q + 10])[0]
            q += 12
            rmethod = data[q]
            q += 2 + data[q + 1]
        datastart = q
        if isdir:
            print("%sDIR %r (%d files) @%d" % (ind, name, numfiles, off))
            if outprefix:
                import os
                os.makedirs(outprefix, exist_ok=True)
                walk(data, datastart, outprefix + "/", depth + 1, numfiles + 2)
            else:
                walk(data, datastart, None, depth + 1, numfiles + 2)
            return
        print("%sENTRY %r d(len=%d comp=%d) r(len=%d comp=%d crc=%04x meth=%s) @%d" % (
            ind, name, datalength, datacomplen, rlen, rcomplen, rcrc, rmethod, off))
        if outprefix:
            import os
            os.makedirs(outprefix, exist_ok=True)
            safe = name.decode("latin1", "replace").replace("/", "-")
            rstart = datastart
            dstart = rstart + rcomplen
            if rcomplen:
                open("%s/%s.rsrc.comp" % (outprefix, safe), "wb").write(data[rstart:dstart])
            if datacomplen:
                open("%s/%s.data.comp" % (outprefix, safe), "wb").write(data[dstart:dstart + datacomplen])
        off = datastart + rcomplen + datacomplen


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "LIST" and len(sys.argv) == 3:
        list_archive(sys.argv[2])
    elif cmd == "EXTRACT" and len(sys.argv) == 4:
        data = open(sys.argv[2], "rb").read()
        i = data.find(b"\x1a\x00\x05")
        base = i + 2
        nf = struct.unpack(">H", data[base + 10:base + 12])[0]
        fo = struct.unpack(">I", data[base + 12:base + 16])[0]
        walk(data, fo, sys.argv[3], 0, nf + 2)
    elif cmd == "DECOMPRESS" and len(sys.argv) == 6:
        method = int(sys.argv[2])
        if method != 13:
            sys.exit("only method 13 implemented")
        data = open(sys.argv[3], "rb").read()
        out = decompress(data, int(sys.argv[4]))
        open(sys.argv[5], "wb").write(out)
        print("decoded %d bytes" % len(out))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
