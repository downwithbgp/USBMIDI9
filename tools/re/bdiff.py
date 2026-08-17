#!/usr/bin/env python3
"""Byte-diff two binaries and report differing runs with offsets.

Usage: bdiff.py FILE_A FILE_B

Prints, for each contiguous differing run: offset range, length, and the
new vs old bytes (EOF when one file ends). Used during the G4 build
gates to prove a rebuilt PEF differs from the previous artifact ONLY in
the expected bytes (e.g. the E1->E2a 4-byte special-main patch, or the
trace-build vs production diff). Promoted from /tmp/bdiff.py
(generalized from hardcoded paths to argv).
"""
import sys


def main():
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    print("a: %s (%d bytes)  b: %s (%d bytes)"
          % (sys.argv[1], len(a), sys.argv[2], len(b)))
    diffs = []
    for i in range(max(len(a), len(b))):
        xa = a[i] if i < len(a) else None
        xb = b[i] if i < len(b) else None
        if xa != xb:
            diffs.append(i)
    runs = []
    for i in diffs:
        if runs and runs[-1][1] + 1 == i:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    for s, e in runs:
        na = a[s:e + 1].hex() if s < len(a) else "EOF"
        nb = b[s:e + 1].hex() if s < len(b) else "EOF"
        print("offset 0x%03x-0x%03x (%d bytes): new=%-30s prev=%s"
              % (s, e, e - s + 1, na, nb))
    if not runs:
        print("identical")


if __name__ == "__main__":
    main()
