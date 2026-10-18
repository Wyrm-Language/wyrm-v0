#!/bin/sh
# The CBOR encoder in src/embed/wyrm/cbor.wy against the wire encoding's Worked
# examples, byte for byte (wy/wyrm/tools/test_cbor_driver.wy). The driver
# imports wyrm::cbor from the binary's embedded images.
set -e
WYRM="$1"
ROOT="$2"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$WYRM" --cache-dir "$WORK/cache" "$ROOT/wy/wyrm/tools/test_cbor_driver.wy" >"$WORK/out" 2>"$WORK/err" || {
    cat "$WORK/out" "$WORK/err" >&2
    echo "wy-cbor: the driver exited nonzero" >&2
    exit 1
}
if grep -q "^FAIL" "$WORK/out" || ! grep -q "^ALL PASS" "$WORK/out"; then
    grep -v "^PASS" "$WORK/out" >&2
    echo "wy-cbor: encoder output differs from the worked examples" >&2
    exit 1
fi
echo "wy-cbor: $(grep -c '^PASS' "$WORK/out") checks"
