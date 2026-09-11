#!/usr/bin/env python3
"""Scan a 68K blob for FreeMIDI-style direct call and +0x118 evidence.

This deliberately knows no proprietary file names or resource contents.  It
is a small reproducibility aid for a disassembly already extracted by the
analyst: it hashes the input, prints instructions touching the common driver
object slot, and annotates nearby selector pushes.

Match-set note: the +0x118 pattern is a raw byte signature (any opcode byte
0x20-0x3F followed by 6A 01 18), so it also matches MOVE.W d16(A2) forms and
opcode bytes in that range that do not decode.  The raw signature list is
the authoritative coverage list and the disassembly column shows what each
hit actually is — treat a hit as a lead, not as proof of a long
displacement.
"""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, Cs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("blob", type=Path)
    args = parser.parse_args()
    data = args.blob.read_bytes()
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN)
    md.detail = False
    # CODE resources contain jump/data areas, so a single linear decode from
    # byte zero is intentionally not used.  Decode a short window at each
    # raw signature instead and retain the raw scan as the authoritative
    # coverage list.
    signatures = []
    for match in re.finditer(rb"[\x20-\x3f]\x6a\x01\x18", data):
        signatures.append((match.start(), "object+0x118 displacement"))
    for match in re.finditer(rb"\x4e\xb9\x00\x03\x8e\x70", data):
        signatures.append((match.start(), "CODE1+0x38e70 direct call"))
    print(f"sha256 {hashlib.sha256(data).hexdigest()}")
    print(f"bytes {len(data)} signatures {len(signatures)}")
    for offset, reason in sorted(signatures):
        decoded = list(md.disasm(data[offset : offset + 12], offset))
        if decoded:
            insn = decoded[0]
            text = f"{insn.mnemonic} {insn.op_str}".strip()
        else:
            text = "<decode failed>"
        print(f"0x{offset:06x}: {reason}: {text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
