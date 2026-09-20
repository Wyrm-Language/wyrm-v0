#!/usr/bin/env bash
# For each corpus fixture, diff `wyrm --disasm`'s mnemonic sequence against
# the compiler's own `.wy_a` listing - a cheap check that the C VM's decoder
# (opcode.h's macros + wy_opcode_names) agrees with wypoc's compiler_bc.opcodes
# table, independent of the rich register/annotation text only the compiler
# can render. Needs only the built `wyrm` binary and the checked-in `.wy_a`
# files: no Python.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
wyrm="${1:-$repo_root/buildDir/src/wyrm/wyrm}"
manifest="$repo_root/test/bytecode/manifest.txt"

if [[ ! -x "$wyrm" ]]; then
    echo "check_disasm.sh: $wyrm not built; run meson compile -C buildDir first" >&2
    exit 1
fi
if [[ ! -f "$manifest" ]]; then
    echo "check_disasm.sh: $manifest missing; run scripts/build_corpus.py first" >&2
    exit 1
fi

checked=0
mismatches=0

# The manifest's REFUSED rows (wyc-path "-") have no .wyc to disassemble.
while IFS=$'\t' read -r name wyc_path _out_path _status _reason; do
    [[ "$wyc_path" == "-" ]] && continue

    wya_path="$repo_root/${wyc_path%.wyc}.wy_a"
    [[ -f "$wya_path" ]] || continue

    # SECTION code's instruction lines look like:
    #   0008: 43 00 00 00                                     ; gset g0 <- L0   (Shape)
    # A bare "; module init" or "; hello.wy:3" comment line has no leading
    # hex offset and is not one, so it is skipped along with every other
    # section.
    expected=$(awk '
        /^SECTION code/ { insection = 1; next }
        /^SECTION / { insection = 0 }
        insection && /^[0-9A-Fa-f]+: .*; / {
            split($0, halves, "; ")
            split(halves[2], words, " ")
            print words[1]
        }
    ' "$wya_path")

    actual=$("$wyrm" "$repo_root/$wyc_path" --disasm | awk '{print $2}')

    checked=$((checked + 1))
    if [[ "$expected" != "$actual" ]]; then
        echo "MISMATCH: $name"
        diff <(echo "$expected") <(echo "$actual") | sed 's/^/    /' || true
        mismatches=$((mismatches + 1))
    fi
done < "$manifest"

echo "check_disasm.sh: checked $checked fixtures, $mismatches mismatches"
[[ "$mismatches" -eq 0 ]]
