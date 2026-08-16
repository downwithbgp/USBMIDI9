#!/bin/sh
# Host test for tools/omdvdata.c: compile it with the repo's strict C89
# flags, run it on the fixture PEF, and verify the four OMdvData
# validations (docs/g4-handoff.md, "The 'OMdv' resource"):
#   - raw PEF begins with "Joy!peffpwpc"
#   - OMdvData size = PEF size + 4
#   - OMdvData[0:4] = big-endian PEF size
#   - OMdvData[4:16] = "Joy!peffpwpc"
# Plus the failure path: a non-PEF input must exit non-zero.
set -e
cd "$(dirname "$0")/.."

PEF=fixtures/omdvdata/pef-fixture.bin
OUT=build/OMdvData.test
BAD=build/not-a-pef.bin

mkdir -p build
CC=${CC:-cc}
$CC -std=c89 -Wall -Wextra -Wpedantic -Werror -O2 -o build/omdvdata tools/omdvdata.c

./build/omdvdata "$PEF" "$OUT"

PEF_SIZE=$(wc -c < "$PEF")
OUT_SIZE=$(wc -c < "$OUT")

# OMdvData size = PEF size + 4
[ "$OUT_SIZE" -eq $((PEF_SIZE + 4)) ] || { echo "size mismatch"; exit 1; }

# OMdvData[0:4] = big-endian PEF size (00 00 <hi> <lo>)
HDR=$(od -An -tx1 -N4 "$OUT" | tr -d ' \n')
EXPECTED=$(printf '0000%04x' "$PEF_SIZE")
[ "$HDR" = "$EXPECTED" ] || { echo "header mismatch: $HDR != $EXPECTED"; exit 1; }

# OMdvData[4:16] = "Joy!peffpwpc" (12-byte magic at bytes 4..15)
MAGIC=$(dd if="$OUT" bs=1 skip=4 count=12 2>/dev/null)
[ "$MAGIC" = "Joy!peffpwpc" ] || { echo "magic mismatch"; exit 1; }

# Failure path: input without the PEF magic must exit non-zero
printf 'not a pef container at all.........' > "$BAD"
if ./build/omdvdata "$BAD" build/bad.out; then
    echo "bad-magic input unexpectedly accepted"
    exit 1
fi

# One-argument mode: writes OMdvData next to the input, byte-identical
# to the two-argument output
mkdir -p build/omdvdata-one-arg
cp "$PEF" build/omdvdata-one-arg/pef.bin
./build/omdvdata build/omdvdata-one-arg/pef.bin >/dev/null
cmp build/omdvdata-one-arg/OMdvData "$OUT" || { echo "one-arg output mismatch"; exit 1; }

# Oversize rejection: valid magic but PEF > 65535 (OMS 16-bit header)
(printf 'Joy!peffpwpc'; head -c 65524 /dev/zero) > build/big-pef.bin
if ./build/omdvdata build/big-pef.bin build/big.out; then
    echo "oversize PEF unexpectedly accepted"
    exit 1
fi

echo "check-omdvdata: OK (size=$OUT_SIZE, header=$HDR, magic verified)"
