#!/bin/sh
# Epic 11 M2/M6: a .wy entry compiles in-process through the embedded
# compiler and its output matches the same script run from the committed
# .wyc. The M6 exit criterion at the end proves self-sufficiency: with a
# PATH that provably reaches no Python (and nothing else), the binary still
# compiles and runs the script from source.
set -e
WYRM="$1"
ROOT="$2"
a=$("$WYRM" -I"$ROOT/test/bytecode" "$ROOT/test/bytecode/hello.wyc")
b=$("$WYRM" -I"$ROOT/test/bytecode" "$ROOT/test/bytecode/hello.wy")
if [ "$a" != "$b" ]; then
    echo "wy-entry: .wy output differs from .wyc output" >&2
    exit 1
fi

# The exit criterion (epic 11): an empty PATH directory reaches no Python
# (asserted loudly first - the point is proving the binary never needs it).
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/bin"
if env -i PATH="$WORK/bin" /bin/sh -c 'command -v python3 >/dev/null 2>&1 || command -v python >/dev/null 2>&1'; then
    echo "wy-entry: cannot prove Python is unreachable on the stripped PATH" >&2
    exit 1
fi
OUT3=$(env -i PATH="$WORK/bin" "$WYRM" -I"$ROOT/wy" "$ROOT/test/bytecode/hello.wy")
[ "$OUT3" = "Hello World" ]

echo "wy-entry: $b (also runs under a Python-free PATH)"
