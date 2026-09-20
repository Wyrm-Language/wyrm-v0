#!/bin/sh
# The REPL (`wyrm -i`, doc/repl-plan.md): each test/corpus/repl/NAME.in is piped
# to the interpreter and its combined stdout+stderr must equal NAME.out.
# Local to this repository - the REPL is a development tool and is not
# cross-checked against any other interpreter. Prompts are only printed on a
# terminal, so a transcript is just the output.
set -u
WYRM="$1"
ROOT="$2"
n=0
failed=0
for in_file in "$ROOT"/test/corpus/repl/*.in; do
    name=$(basename "$in_file" .in)
    expected="$ROOT/test/corpus/repl/$name.out"
    actual=$(timeout 120 "$WYRM" -i < "$in_file" 2>&1)
    rc=$?
    if [ "$rc" != 0 ]; then
        echo "wy-repl: $name exited $rc" >&2
        failed=1
    fi
    if [ "$actual" != "$(cat "$expected")" ]; then
        echo "wy-repl: $name output differs" >&2
        printf '%s\n' "$actual" | diff "$expected" - >&2
        failed=1
    fi
    n=$((n + 1))
done
[ "$n" -gt 0 ] || { echo "wy-repl: no transcripts found" >&2; exit 1; }
[ "$failed" = 0 ] || exit 1

# The reservation limit: with a tiny code reservation (WYRM_SESSION_CODE_WORDS)
# some input is refused with a clear message, the session survives, and :reset
# starts fresh. Generated here (the exact input that overflows depends on the
# code generator, so the check is on the shape of the output, not the line).
limit_out=$( { i=0; while [ "$i" -lt 60 ]; do echo 'println(1)'; i=$((i + 1)); done
               echo ':reset'; echo 'println("fresh")'; } \
             | WYRM_SESSION_CODE_WORDS=60 timeout 120 "$WYRM" -i 2>&1 )
case "$limit_out" in *"session limit reached"*) ;; *) echo "wy-repl: no limit message" >&2; exit 1;; esac
[ "$(printf '%s\n' "$limit_out" | head -n 1)" = "1" ] || { echo "wy-repl: early inputs must run" >&2; exit 1; }
[ "$(printf '%s\n' "$limit_out" | tail -n 1)" = "fresh" ] || { echo "wy-repl: :reset must recover" >&2; exit 1; }

# -i takes no script and refuses to combine with the other modes.
if "$WYRM" -i somefile.wy >/dev/null 2>&1; then
    echo "wy-repl: -i with a script should be a usage error" >&2
    exit 1
fi
echo "wy-repl: $n transcripts match"
