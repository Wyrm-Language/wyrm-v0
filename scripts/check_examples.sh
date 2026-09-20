#!/bin/sh
# Run each embedding example (examples/) and compare its output with
# examples/expected/NAME.out. The two REPL examples read NAME.in on stdin.
# usage: check_examples.sh EXAMPLES_DIR basic script repl custom_repl
set -u
DIR="$1"; BASIC="$2"; SCRIPT="$3"; REPL="$4"; CUSTOM="$5"
failed=0

check() {  # check NAME COMMAND [INPUT_FILE]
    name="$1"; cmd="$2"; input="${3:-/dev/null}"
    actual=$(timeout 120 "$cmd" < "$input" 2>&1)
    if [ "$actual" != "$(cat "$DIR/expected/$name.out")" ]; then
        echo "examples: $name output differs" >&2
        printf '%s\n' "$actual" | diff "$DIR/expected/$name.out" - >&2
        failed=1
    fi
}

check embed_basic "$BASIC"
check embed_script "$SCRIPT"
check embed_repl "$REPL" "$DIR/expected/embed_repl.in"
check embed_custom_repl "$CUSTOM" "$DIR/expected/embed_custom_repl.in"

[ "$failed" = 0 ] || exit 1
echo "examples: 4 examples match"
