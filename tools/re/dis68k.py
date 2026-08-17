#!/usr/bin/env python3
"""Disassemble a region of a 68K blob (e.g. the OMS 2.3.8 library PROC 1).

Usage: dis68k.py FILE START_HEX LENGTH
  FILE    - raw 68K code blob (e.g. omslib_proc1.bin, extracted from
            'Open Music System.rsrc' PROC 1 with rsrc_list.py)
  START   - byte offset into the blob (hex, e.g. 0x99b6)
  LENGTH  - number of bytes to disassemble (hex or decimal)

Requires capstone (pip install capstone). Promoted from /tmp/dis68k.py
(2026-08-17 OMS 2.3.8 RE session); used to produce the library map in
docs/re/oms-2.3.8-map.md.
"""
import sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

path, start, length = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
data = open(path, "rb").read()
md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
md.detail = True
for insn in md.disasm(data[start:start + length], start):
    print("0x%05x: %-24s %s" % (insn.address, insn.mnemonic, insn.op_str))
