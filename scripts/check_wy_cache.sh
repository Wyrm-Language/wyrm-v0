#!/bin/sh
# Epic 11 M3: the .wyd cache, --cache-dir, and -v resolution lines.
#
# 1. First run compiles (jit) and writes <dir>/__wycache__/<name>.wyd.
# 2. Second run loads the cache; no recompile, same output.
# 3. Touching the source invalidates the cache (jit again, cache stale).
# 4. --cache-dir DIR maps to DIR/<abs dir of source>/<name>.wyd, with no
#    __wycache__ directory anywhere.
# 5. A Python-produced .wyc in the cache directory is ignored (only .wyd
#    is consulted), so the toolchains never share caches.
# 6. A cache .wyd from another format version (a different WY_IMAGE_VERSION,
#    or the old BSON container) is a miss: recompiled and rewritten, not an
#    error. An explicitly named .wyc/.wyd of another version is an error.
set -e
WYRM="$1"
ROOT="$2"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp "$ROOT/test/corpus/hello.wy" "$WORK/hello.wy"

OUT=$("$WYRM" -v "$WORK/hello.wy" 2>"$WORK/err1")
grep -q "jit $WORK/hello.wy (cache absent)" "$WORK/err1"
grep -q "cache write $WORK/__wycache__/hello.wyd" "$WORK/err1"
[ -f "$WORK/__wycache__/hello.wyd" ]
[ "$OUT" = "Hello World" ]

OUT2=$("$WYRM" -v "$WORK/hello.wy" 2>"$WORK/err2")
grep -q "cache $WORK/__wycache__/hello.wyd" "$WORK/err2"
if grep -q "jit" "$WORK/err2"; then
    echo "wy-cache: the second run recompiled" >&2
    exit 1
fi
[ "$OUT2" = "Hello World" ]

sleep 1
touch "$WORK/hello.wy"
"$WYRM" -v "$WORK/hello.wy" >/dev/null 2>"$WORK/err3"
grep -q "jit $WORK/hello.wy (cache stale)" "$WORK/err3"

# --cache-dir: prefix mapping for reads and writes; no __wycache__ created.
rm -rf "$WORK/cache" "$WORK/__wycache__"
"$WYRM" -v --cache-dir "$WORK/cache" "$WORK/hello.wy" >/dev/null 2>"$WORK/err4"
[ -f "$WORK/cache$WORK/hello.wyd" ]
[ ! -e "$WORK/__wycache__" ]
"$WYRM" -v --cache-dir "$WORK/cache" "$WORK/hello.wy" >/dev/null 2>"$WORK/err5"
grep -q "cache $WORK/cache$WORK/hello.wyd" "$WORK/err5"

# A .wyc in the cache directory is never consulted (epic 11 contract).
mkdir -p "$WORK/__wycache__"
printf 'not an image' > "$WORK/__wycache__/hello.wyc"
OUT6=$("$WYRM" -v "$WORK/hello.wy" 2>"$WORK/err6")
grep -q "jit $WORK/hello.wy" "$WORK/err6"
[ "$OUT6" = "Hello World" ]

# A cache file in another format version is a miss, not an error.
CACHE="$WORK/__wycache__/hello.wyd"
rm -rf "$WORK/__wycache__"
"$WYRM" "$WORK/hello.wy" >/dev/null
cp "$CACHE" "$WORK/good.wyd"
stale_check() {
    "$WYRM" -v "$WORK/hello.wy" >"$WORK/out7" 2>"$WORK/err7"
    grep -q "jit $WORK/hello.wy (cache stale)" "$WORK/err7"
    [ "$(cat "$WORK/out7")" = "Hello World" ]
    cmp -s "$CACHE" "$WORK/good.wyd"
}
# the version bytes (offsets 7..10 of the preamble), +1 on the last
cp "$WORK/good.wyd" "$CACHE"
printf '\141' | dd of="$CACHE" bs=1 seek=10 conv=notrunc 2>/dev/null
stale_check
# the old BSON container: "WYC\0", version 1
printf 'WYC\000\001\000\000\000' > "$CACHE"
stale_check
# a truncated cache
head -c 40 "$WORK/good.wyd" > "$CACHE"
stale_check
# an empty cache
: > "$CACHE"
stale_check

# An explicitly named container of another version is an error, with a
# message and a nonzero exit.
cp "$WORK/good.wyd" "$WORK/named.wyd"
printf '\141' | dd of="$WORK/named.wyd" bs=1 seek=10 conv=notrunc 2>/dev/null
if "$WYRM" "$WORK/named.wyd" >/dev/null 2>"$WORK/err8"; then
    echo "wy-cache: a stale named .wyd ran" >&2
    exit 1
fi
grep -q "failed to load module" "$WORK/err8"
printf 'WYC\000\001\000\000\000' > "$WORK/old.wyc"
if "$WYRM" "$WORK/old.wyc" >/dev/null 2>"$WORK/err9"; then
    echo "wy-cache: an old BSON .wyc ran" >&2
    exit 1
fi
grep -q "failed to load module" "$WORK/err9"
# ... and the current one runs.
[ "$("$WYRM" "$WORK/good.wyd")" = "Hello World" ]

echo "wy-cache: jit, cache hit, touch-stale, --cache-dir prefix, .wyc ignored, stale version recompiled"
