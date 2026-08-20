#!/usr/bin/env bash
# Smoke tests for tools/re — run each promoted tool on a repo fixture (or
# a tiny synthetic fixture) and assert the expected output. Requires:
#   python3 + capstone (for dis68k.py/disppc.py; skipped if missing)
#   cc (for procinfo_check.c)
# Usage: tools/re/smoke.sh   (also reachable as `make check-re-tools`)
set -u
cd "$(dirname "$0")"
FIX=fixtures
PEF=../../pefcheck/fixtures
FAIL=0

say() { printf '%s\n' "$*"; }
ok() { say "  PASS: $*"; }
bad() { say "  FAIL: $*"; FAIL=1; }

say "== dis68k.py (synthetic 68K: move.l #1,d0; rts) =="
if python3 -c "import capstone" 2>/dev/null; then
    OUT=$(python3 dis68k.py $FIX/m68k_probe.bin 0 8)
    echo "$OUT" | grep -q "move" && echo "$OUT" | grep -q "rts" && ok "dis68k" || bad "dis68k: $OUT"
else
    say "  SKIP: capstone not installed"
fi

say "== disppc.py (production PEF code section 0x280) =="
if python3 -c "import capstone" 2>/dev/null; then
    OUT=$(python3 disppc.py $PEF/production_usbmidi9.pef 0x280 16)
    echo "$OUT" | grep -q "addi" && ok "disppc" || bad "disppc: $OUT"
else
    say "  SKIP: capstone not installed"
fi

say "== pef_unpack.py (production PackedData: 269 -> 468, COMPLETE) =="
OUT=$(python3 pef_unpack.py $PEF/production_usbmidi9.pef 0x2410 269 468)
echo "$OUT" | grep -q "COMPLETE" && ok "production unpack" || bad "production unpack: $OUT"

say "== pef_unpack.py (authentic TM PackedData: 75 -> 167, COMPLETE) =="
OUT=$(python3 pef_unpack.py $PEF/tm_ppcc1.pef 0x5e0 75 167)
echo "$OUT" | grep -q "COMPLETE" && ok "TM unpack" || bad "TM unpack: $OUT"

say "== pef_unpack.py (multi-byte BIG-ENDIAN varint: 0x00 0x81 0x48 -> 200 zeros) =="
OUT=$(python3 pef_unpack.py $FIX/varint_probe.bin 0 3 200)
echo "$OUT" | grep -q "COMPLETE" && ok "varint endianness" || bad "varint endianness: $OUT"

say "== pef_loaderinfo.py (production: mainSection=-1, export main) =="
OUT=$(python3 pef_loaderinfo.py $PEF/production_usbmidi9.pef)
echo "$OUT" | grep -q "mainSection=-1" && echo "$OUT" | grep -q "name=b'main'" && ok "loaderinfo" || bad "loaderinfo: $OUT"

say "== pef_analyze.py (production: 3 libs, 13 imports, USBManagerLib) =="
OUT=$(python3 pef_analyze.py $PEF/production_usbmidi9.pef)
echo "$OUT" | grep -q "USBManagerLib" && echo "$OUT" | grep -q "CallUniversalProc" && echo "$OUT" | grep -q "main" && ok "pef_analyze" || bad "pef_analyze: $OUT"

say "== ppcc_abi_report.py (function-main/vector versus TM and RD2) =="
OUT=$(python3 ppcc_abi_report.py ../../USBMIDI9/USBMIDI9_OMS.production-save \
    --control $PEF/tm_ppcc1.pef --rejected ../../USBMIDI9/USBMIDI9_OMS_RD2)
echo "$OUT" | grep -q "OMS ProcInfo: 0x00000fb0" && \
echo "$OUT" | grep -q "main-representation=transition-vector" && \
echo "$OUT" | grep -q "rejected-experiment.*" && \
echo "$OUT" | grep -q "descriptor-main-status=REJECTED" && \
echo "$OUT" | grep -q "DIAGNOSTIC: PASS" && ok "ppcc ABI report" || bad "ppcc ABI report: $OUT"

say "== rsrc_list.py (synthetic resource fork: TEST 128) =="
OUT=$(python3 rsrc_list.py $FIX/rsrcfork.bin)
echo "$OUT" | grep -q "TEST 128" && ok "rsrc_list" || bad "rsrc_list: $OUT"

say "== appledouble.py (synthetic AppleDouble: resource-fork entry) =="
OUT=$(python3 appledouble.py $FIX/appledouble.bin)
echo "$OUT" | grep -q "magic=0x00051607" && echo "$OUT" | grep -q "resource-fork" && ok "appledouble" || bad "appledouble: $OUT"

say "== omsabi.py (OMSDevice mac68k sizeof = 182 = 0xB6) =="
OUT=$(python3 omsabi.py)
echo "$OUT" | grep -q "sizeof(OMSDevice) = 182 = 0xb6" && ok "omsabi" || bad "omsabi: $OUT"

say "== procinfo_check.c (compile + run: 0xFB0 / 0x2F0 decodes) =="
if command -v cc >/dev/null; then
    TMP=$(mktemp /tmp/procinfo_check.XXXXXX)
    cc -o "$TMP" procinfo_check.c && OUT=$("$TMP")
    echo "$OUT" | grep -q "uppOMSDriverProcInfo      = 0xFB0" && \
    echo "$OUT" | grep -q "uppOMSDvrAdd1DevProc1Info = 0x2F0" && \
    echo "$OUT" | grep -q "OK" && ok "procinfo_check" || bad "procinfo_check: $OUT"
    rm -f "$TMP"
else
    say "  SKIP: no C compiler"
fi

say "== bdiff.py (E1 vs E2a: exactly the 4-byte special-main patch) =="
OUT=$(python3 bdiff.py $PEF/e1_oms.pef $PEF/e2a_oms.pef)
echo "$OUT" | grep -q "offset 0x080-0x083 (4 bytes)" && ok "bdiff" || bad "bdiff: $OUT"

say "== artifacts.toml (parses; every in-repo sha256 matches) =="
OUT=$(python3 - <<'PYEOF'
import tomllib, hashlib, os
doc = tomllib.load(open('../../docs/re/artifacts.toml','rb'))
n = 0
bad = []
missing = []
for a in doc['artifact']:
    if 'path' not in a:
        continue
    p = a['path'] if os.path.isabs(a['path']) else '../../' + a['path']
    if not os.path.isfile(p):
        missing.append(a['name'])   # external/hash-only artifact not on this host
        continue
    n += 1
    got = hashlib.sha256(open(p,'rb').read()).hexdigest()
    if got != a['sha256']:
        bad.append(a['name'])
if bad:
    print('MISMATCH:', bad)
    raise SystemExit(1)
print('manifest ok, in-repo artifacts verified:', n)
if missing:
    print('skipped (not present on this host):', ', '.join(missing))
PYEOF
)
echo "$OUT" | grep -q "manifest ok" && ok "artifacts.toml" || bad "artifacts.toml: $OUT"

if [ "$FAIL" -eq 0 ]; then
    say "check-re-tools: ALL PASS"
    exit 0
else
    say "check-re-tools: FAILURES"
    exit 1
fi
