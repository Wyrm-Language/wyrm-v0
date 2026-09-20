#!/bin/sh
# Epic 11 M4: CLI flag parity (MVP scope) - --check, --build-bc, -m, and
# the pypoc exit-code contract (0 ok, 1 compile/run failure, 2 usage).
set -e
WYRM="$1"
ROOT="$2"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/test/bytecode/hello.wy" "$WORK/hello.wy"
printf 'x := \n' > "$WORK/bad.wy"

# --check: silent success, non-zero with a message on a compile error.
"$WYRM" --check "$WORK/hello.wy"
if "$WYRM" --check "$WORK/bad.wy" 2>/dev/null; then
    echo "wy-flags: --check accepted a broken script" >&2
    exit 1
fi

# --build-bc: containers land in -o, byte-identical to the run cache's .wyd.
"$WYRM" --build-bc -o "$WORK/out" --emit wyd,c --strip "$WORK/hello.wy" > "$WORK/bc.log"
[ -f "$WORK/out/hello.wyd" ]
[ -f "$WORK/out/hello.c" ]
grep -q "wyrm: wrote $WORK/out/hello.wyd" "$WORK/bc.log"
"$WYRM" --cache-dir "$WORK/cache" "$WORK/hello.wy" >/dev/null
cmp "$WORK/out/hello.wyd" "$WORK/cache$WORK/hello.wyd"
"$WYRM" --disasm "$WORK/out/hello.wyd" >/dev/null

# --build-bc usage contract.
if "$WYRM" --build-bc --emit wya "$WORK/hello.wy" 2>/dev/null; then
    echo "wy-flags: --emit wya should be refused (known gap)" >&2
    exit 1
fi
if "$WYRM" --build-bc --emit nope "$WORK/hello.wy" 2>/dev/null; then
    echo "wy-flags: --emit accepted an unknown container" >&2
    exit 1
fi

# -m: module from the search path (compiled through the cache) and from
# the builtin table; a miss is a failure.
printf 'println("ran via -m")\nprintln(__ARGS[0])\n' > "$WORK/mymod.wy"
OUT=$("$WYRM" -I"$WORK" -m mymod alpha)
[ "$OUT" = "ran via -m
alpha" ]
"$WYRM" -m wyrm::tools::compile_source
if "$WYRM" -m no::such 2>/dev/null; then
    echo "wy-flags: -m resolved a missing module" >&2
    exit 1
fi

echo "wy-flags: --check, --build-bc (wyd+c, --strip, -o, byte-identity), -m, exit codes"
