#!/usr/bin/env python3
"""Disassemble PPC code at a PEF container offset (big-endian 32-bit).

Usage: disppc.py FILE CONTAINER_OFFSET LENGTH
  FILE    - raw PEF container (e.g. pefcheck/fixtures/production_usbmidi9.pef)
  OFFSET  - byte offset into the FILE (the PEF container offset, hex,
            e.g. 0x280 = code section base of the production build)
  LENGTH  - number of bytes to disassemble

NOTE: addresses are CONTAINER-RELATIVE (the capstone base = the offset you
pass). This is deliberate: CFM fragments move between boots, so all RE
offsets are container-relative (see docs/re/README.md conventions).
Requires capstone. Promoted from /tmp/disppc.py.
"""
import sys
from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32

path, off, length = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
data = open(path, "rb").read()
md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN | CS_MODE_32)
md.detail = True
for insn in md.disasm(data[off:off + length], off):
    print("0x%06x: %-28s %s" % (insn.address, insn.mnemonic, insn.op_str))
