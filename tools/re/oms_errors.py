#!/usr/bin/env python3
"""Extract OMS error constants from the SDK OMS.h header.

Usage: oms_errors.py OMS_H_PATH

Prints every kOMS*/oms*Err/OMS*Err constant definition from the header,
then the omsNoErr paragraph from the OMS spec text (optional: pass a
second argument with the spec text path). Used to cross-check OMSErr
values during driver work. Promoted from /tmp/oms_errors.py
(generalized from hardcoded paths to argv).
"""
import re
import sys


def main():
    hdr = open(sys.argv[1], 'rb').read()
    t = hdr.decode('latin-1')
    for m in re.finditer(r'(?m)^\s*(kOMS[A-Za-z0-9]*|oms[A-Za-z0-9]*Err|OMS[A-Za-z0-9]*Err)\s*=\s*-?\d+.*$', t):
        print(m.group(0).strip())
    if len(sys.argv) > 2:
        print('---spec---')
        s = open(sys.argv[2], encoding='latin-1').read()
        i = s.find('omsNoErr')
        print(s[max(0, i - 400):i + 300])


if __name__ == '__main__':
    main()
