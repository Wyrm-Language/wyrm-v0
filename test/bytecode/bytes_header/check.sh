#!/usr/bin/env bash
# Epic 7 M5 exit criterion. Builds the BSON `header` document
# { n: "hello", v: 1, g: 2, l: 3 } by hand using bytes methods, once on
# each engine, and checks both against pypoc's own bsonlite encoder,
# byte-for-byte.
#
# Two .wy sources exist because pypoc and the C VM currently have
# incompatible std::io surfaces (pypoc: a compiled File class; the C VM:
# raw POSIX natives on an int handle, since it doesn't load/compile .wy
# corelib sources yet - no compiler exists in this repo until epics
# 9-11). Everything except the last three lines of each script is
# byte-for-byte identical; diff them to confirm if in doubt.
#
# This is a standalone script, not wired into `meson test`/pytest,
# because both existing harnesses compare stdout against a golden .out
# file and this fixture's result is a written file, not stdout - epic 7's
# own plan explicitly allows this ("a standalone script is acceptable,
# with a note for epic 9 to formalize").
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
here="$repo_root/test/bytecode/bytes_header"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cd "$work"

"$repo_root/pypoc/.venv/bin/wyrm" -I "$repo_root/wy" "$here/pypoc_header.wy"
mv bytes_header.bin pypoc.bin

# --build-bc always writes its outputs alongside the *input* file, ignoring
# -o's directory component - copy the source into $work first so nothing
# is written back into the repo tree.
cp "$here/cvm_header.wy" cvm_header.wy
"$repo_root/pypoc/.venv/bin/wyrm" -I "$repo_root/wy" --build-bc cvm_header.wy >/dev/null
"$repo_root/buildDir/src/wyrm/wyrm" cvm_header.wyc
mv bytes_header.bin cvm.bin

"$repo_root/pypoc/.venv/bin/python3" -c "
import sys
sys.path.insert(0, '$repo_root/pypoc')
from wypoc.compiler_bc import bsonlite
open('reference.bin', 'wb').write(bsonlite.encode_document({'n': 'hello', 'v': 1, 'g': 2, 'l': 3}))
"

cmp pypoc.bin reference.bin && echo "OK: pypoc matches bsonlite.encode_document"
cmp cvm.bin reference.bin && echo "OK: C VM matches bsonlite.encode_document"
echo "PASS"
