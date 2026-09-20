#!/bin/sh
# The `trap([code])` intrinsic: lowers to the trap opcode, faults the run with a
# message per code (default 1 "debugger break"), is shadowable by a user
# binding, and needs an integer literal 0-255.
set -u
WYRM="$1"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
n=0
run() {  # run <source>; sets OUT (stdout), ERR (stderr), RC
    n=$((n + 1))
    printf '%s\n' "$1" > "$WORK/t$n.wy"
    OUT=$("$WYRM" --cache-dir "$WORK/c$n" "$WORK/t$n.wy" 2> "$WORK/err$n")
    RC=$?
    ERR=$(cat "$WORK/err$n")
}
fail() { echo "wy-trap: $1" >&2; echo "  rc=$RC out=[$OUT] err=[$ERR]" >&2; exit 1; }

run 'println("before")
trap()
println("unreachable")'
[ "$RC" = 1 ] || fail "trap() should fail the run"
[ "$OUT" = "before" ] || fail "trap() must stop execution"
case "$ERR" in *"debugger break"*) ;; *) fail "default code is a debugger break";; esac

run 'trap(7)'
case "$ERR" in *"trap 7"*) ;; *) fail "code 7 reports 'trap 7'";; esac

run 'trap(0)'
case "$ERR" in *unreachable*) ;; *) fail "code 0 is the unreachable trap";; esac

run 'fn f(x):
    if x > 0:
        return "pos"
    trap(9)
println(f(1))
f(0)'
[ "$OUT" = "pos" ] || fail "a trap on a branch not taken must not fire"
case "$ERR" in *"trap 9"*) ;; *) fail "the taken trap fires";; esac

run 'fn trap():
    return "mine"
println(trap())'
[ "$RC" = 0 ] && [ "$OUT" = "mine" ] || fail "a user function named trap shadows the intrinsic"

run 'trap(300)'
[ "$RC" != 0 ] && case "$ERR" in *"0-255"*) ;; *) fail "out-of-range code is a compile error";; esac
run 'x := 3
trap(x)'
[ "$RC" != 0 ] && case "$ERR" in *"0-255"*) ;; *) fail "a non-literal code is a compile error";; esac
run 'trap(1, 2)'
[ "$RC" != 0 ] || fail "two arguments are a compile error"

echo "wy-trap: default, coded, branch, shadowing and argument checks"
