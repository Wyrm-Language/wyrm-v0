#!/bin/sh
# Epic 11 M2/M6: a .wy entry compiles in-process through the embedded
# compiler and its output matches the same script run from a .wyd built by
# --build-bc. The M6 exit criterion at the end proves self-sufficiency: with a
# PATH that provably reaches no Python (and nothing else), the binary still
# compiles and runs the script from source.
set -e
WYRM="$1"
ROOT="$2"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/test/corpus/hello.wy" "$WORK/hello.wy"
"$WYRM" --build-bc -o "$WORK" --emit wyd "$WORK/hello.wy" >/dev/null
mkdir "$WORK/img" && mv "$WORK/hello.wyd" "$WORK/img/hello.wyd"
a=$("$WYRM" "$WORK/img/hello.wyd")
b=$("$WYRM" "$ROOT/test/corpus/hello.wy")
if [ "$a" != "$b" ]; then
    echo "wy-entry: .wy output differs from .wyd output" >&2
    exit 1
fi

# The exit criterion (epic 11): an empty PATH directory reaches no Python
# (asserted loudly first - the point is proving the binary never needs it).
mkdir "$WORK/bin"
if env -i PATH="$WORK/bin" /bin/sh -c 'command -v python3 >/dev/null 2>&1 || command -v python >/dev/null 2>&1'; then
    echo "wy-entry: cannot prove Python is unreachable on the stripped PATH" >&2
    exit 1
fi
OUT3=$(env -i PATH="$WORK/bin" "$WYRM" "$ROOT/test/corpus/hello.wy")
[ "$OUT3" = "Hello World" ]

echo "wy-entry: $b (also runs under a Python-free PATH)"
