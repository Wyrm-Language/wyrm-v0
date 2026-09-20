# Epic 9 — bjson and image writer in wyrm

## Goal

Give wyrm the ability to write its own binary module images, with no Python involved in
the encoding path. Two new wy modules, `wy/wyrm/bjson.wy` and `wy/wyrm/image.wy`, plus a
generated `wy/wyrm/opcodes.wy`, reproduce what `pypoc/wypoc/compiler_bc/bsonlite.py` and
`image.py` do in Python, but written against the `bytes` type landed in epic 7. This is
the last epic before the compiler itself moves to wyrm (epic 10): epic 10 will call these
modules to serialize the images it builds, so they must be exactly byte-compatible with
pypoc's containers first, proven by hand-assembling one image (`hello`) rather than by
compiling one, since there is no wyrm-side compiler yet.

The three wy files are themselves compiled by pypoc's compiler (`wypoc/compiler_bc`,
already targets the C VM's opcode set since epic 1) and run on this repo's `wyrm` binary
(epic 6). Nothing here runs on pypoc's tree-walking interpreter as the system under test;
pypoc is only the cross-compiler for bootstrapping wy source into bytecode the C VM can
execute, exactly the role epic 10's "running engine for the port" section assumes.

**Exit criterion:**
```sh
pypoc/.venv/bin/wyrm -Iwy -Iwy/wyrm --build-bc -o /tmp/e9 --emit wyc wy/wyrm/tools/build_hello.wy
./buildDir/src/wyrm/wyrm -Iwy /tmp/e9/build_hello.wyc /tmp/e9/hello_out.wyc
cmp /tmp/e9/hello_out.wyc pypoc/test/bytecode/hello.wyc
```
expected: `cmp` reports no difference (see "Assumptions" for the debug-section caveat,
which changes this to a `--strip`-produced reference rather than the checked-in file).

## Inputs

- `vm_plan/epic_8_report.md` if epic 8 has run (front-end cleanup: `ast.wy`, `decode.wy`
  fixes, parser parity). If absent, read the plan's Epic 8 section instead and treat its
  deliverables as unverified; note any that epic 9 actually needs (bug-free `\x`/`\u`
  decoding in `decode.wy` is used nowhere in this epic, so its absence is not blocking).
- `vm_plan/epic_7_report.md` if it exists (the `bytes` type landing pypoc + C); otherwise
  the plan's Epic 7 section (`bytes(n)`, `append`, `resize`, `slice`, `to_str`,
  `pack_u8/i32/u32/f32/f64`, `unpack_*`, `copy`, `==`).

State-scan checklist (confirm before starting; the codebase has moved since plan time):
1. Does `wy_type_tag_bytes` / `bytes` exist and pass its own tests on both pypoc and the C
   VM (`meson test -C buildDir --test-case="*bytes*"`)? If not, epic 9 is blocked, not
   just epic 7 incomplete-tolerant.
2. Does `wy/wyrm/` already have a `bjson.wy`, `image.wy`, or `opcodes.wy`? (none existed
   at plan time; confirm still true with `ls wy/wyrm/`.)
3. Re-read `pypoc/wypoc/compiler_bc/bsonlite.py` and `image.py` in full; the plan's line
   numbers below are as of commit-time and may have drifted (the file sizes cited in the
   approved plan, e.g. "image.py 808 lines," should still roughly match `wc -l`).
4. Confirm `pypoc/wypoc/compiler_bc/opcodes.py`'s `OPS` table and `c_header()` are still
   the single source of truth (`tools/generate_opcode_header.py` regenerates
   `wypoc/compiler_bc/include/wyrm/opcode.h` and a test asserts freshness, find that
   test, likely `test_compiler_bc_format.py`).
5. Confirm section ids: `pypoc/wypoc/compiler_bc/include/wyrm/image.h`'s `WY_SEC_*` enum
   should read `WY_SEC_HEADER=1` through `WY_SEC_FREE=11` (11 sections, ids 1-11, not a
   higher range). The table lives at `pypoc/wypoc/compiler_bc/image.py:29-43`
   (`SECTION_IDS`); the enum and that table must agree.
6. Confirm `pypoc/test/bytecode/hello.wyc` still exists and still matches the byte dump
   in `pypoc/doc/wyc-format.md` Appendix A (7 sections: header, statics, functions, code,
   debug, exports, free, no slot_defaults, symbols, classes, messages).
7. Check whether `pypoc/test/bytecode/hello_1.c` (an older `.c`-embedding example) matches
   the current `to_c()` output shape; at scan time it still carried a `relocations`
   section and a `symbols` section holding `"print"`, both retired ideas not in the
   current 11-section format, so it is stale and must not be used as a template.
8. Confirm the C VM's loader (epic 1/2) can already read a `.wyc` and that `wyrm file.wyc`
   (no compile step) runs it, per epic 5/6's CLI work, needed for the exit criterion's
   second command.
9. Run `meson test -C buildDir` and record pass count for the report's "before" line.

## Context to load

Read (not grep) in this order:
1. `pypoc/doc/wyc-format.md` §2 (container layout, lines ~110-158), §4 (BSON subset,
   ~186-249), §5 (instruction encoding, ~250-361), §8 (section schemas, ~594-815),
   Appendix A (~893-985, the byte-by-byte `hello` walkthrough). ~3.5k tokens.
2. `pypoc/wypoc/compiler_bc/bsonlite.py` in full (223 lines: `encode_document`,
   `encode_array`, `_element`/`_value`/`_cstring` at ~50-115, `decode_document`,
   `_read_document`/`_read_value`/`_read_cstring` at ~120-223). ~2k tokens.
3. `pypoc/wypoc/compiler_bc/image.py`: `SECTION_IDS` (~30-42), `ModuleImage.sections()`
   (~322-352), `code_bytes()` (~354), `to_wyc()` (~478-495), `to_wya()`/`_code_listing()`
   (~496-583), `to_c()` (~504-550), `assemble_wya()` (~622-675), `_wrap_wyc`/`read_wyc`
   (~678-707). ~3k tokens.
4. `pypoc/wypoc/compiler_bc/opcodes.py`: the `OPS` table shape (read the `Op` dataclass
   and one `_core`/`_pair` example, ~40-104, then skim entries rather than reading all
   ~430 rows), `lookup`/`L`/`P`/`is_p`/`reg_index`/`reg_name`/`to_reg8`/`from_reg8`
   (~554-609), `pack`/`pack_pairable` (~622-712), `c_header()` (~848-907). ~2.5k tokens.
5. `tools/generate_opcode_header.py` in full (47 lines), the exact pattern the new
   `generate_opcode_wy.py` copies (read table, diff against checked-in file, write or
   report "already up to date").
6. `include/wyrm/opcode.h` (this repo's copy, adopted verbatim per epic 1), grep only,
   to confirm the enum names `generate_opcode_wy.py` must mirror.
7. `wy/wyrm/_dsl.wy` header comment (~1-70), read only if `image.wy`'s directory-sort /
   alignment logic wants a macro; otherwise plain `fn` code suffices and this can be
   skipped.
8. Epic 7's `bytes` API surface, grep `pack_u8\|pack_i32\|pack_f64\|unpack_` across
   `doc/stdlib.md` or `doc/language-spec.md` once epic 7 has landed, to get exact method
   names (do not guess signatures).

Grep-only, never paste large excerpts: `pypoc/test/bytecode/hello.wy_a` (the disassembly
ground truth for the hand-assembled image), `pypoc/test/test_compiler_bc_*.py` (existing
Python-side tests of the same encoders, useful as a checklist of edge cases: NUL in a
key, int32 overflow, non-dense array keys, truncated document, bad binary subtype).

## Assumptions

- `bytes` (epic 7) is complete enough to provide `append`, `resize`, `slice`, `to_str`,
  `pack_u8`, `pack_i32`, `pack_u32`, `pack_f64`, `unpack_u8`, `unpack_i32`, `unpack_u32`,
  `unpack_f64`, `copy`, `==`, and file write via `std::io` binary mode. *(verify in scan)*
- The BSON subset is exactly the 8 tags in `bsonlite.py` (`0x01,0x02,0x03,0x04,0x05,0x08,
  0x0A,0x10`); wyrm's encoder needs no others and must reject anything else the same way.
  *(verify in scan)*
- Section ids are 1-11 as in `image.h`'s `WY_SEC_*` enum and `image.py:29-43`
  `SECTION_IDS`; the two are in step. *(verify in scan)*
- `pypoc/test/bytecode/hello.wyc` includes a `debug` section (id 9, 45 bytes per Appendix
  A) that the compiler emits from real source positions. wyrm hand-assembling the image
  has no source to record positions for, so exact byte identity against the checked-in
  `hello.wyc` is not achievable without fabricating debug records. **Decision (may be
  revisited by the executor): compare against a *stripped* reference instead** ,
  regenerate it with `pypoc/.venv/bin/wyrm --build-bc --strip --emit wyc
  test/bytecode/hello.wy` and diff that (6 sections: header, statics, functions, code,
  exports, free) rather than the checked-in 7-section file. `wy/wyrm/image.wy` does not
  attempt to emit a debug section in epic 9; that stays deferred (see below).
  *(verify in scan: re-derive the stripped reference's exact bytes before trusting this)*
- `generate_opcode_wy.py` can reuse `opcodes.py`'s `OPS` table and its `c_header()` as a
  template for a `.wy` table (an array/list of records: opcode name, value, form, operand
  shape) rather than re-deriving the instruction set by hand. *(verify in scan)*
- No wyrm-side compiler exists yet (epic 10 not started), so every `.wy` file this epic
  writes is compiled by `pypoc/wypoc/compiler_bc` and *run* on this repo's `wyrm` binary;
  none of it runs on pypoc's tree-walking interpreter as the system under test.
  *(verify in scan)*

## Milestones

### M1 — `wy/wyrm/bjson.wy`: 8-tag encoder and decoder over `bytes`

**Scope.** Port `bsonlite.py` encode/decode to wyrm: `encode_document(pairs)`,
`encode_array(items)`, `encode(value)`, `decode_document(bytes)`, `decode_array(bytes)`,
`decode(bytes)`, plus the framing/element helpers, all operating on the `bytes` object
from epic 7 instead of Python `bytes`/`bytearray`. Preserve the exact validation rules:
NUL-free keys, int32 range check, dense array-index keys, bool-before-int (wyrm has
distinct `bool`/`int` types so this collapses to ordinary dispatch), binary subtype must
be 0, document/string NUL termination checked on decode.

**Files.** New: `wy/wyrm/bjson.wy`. Test: `test/wy/test_wy_bjson.wy` (new), fixtures
comparing encoder output against fixed hex strings taken from `pypoc/test/test_compiler_bc_bsonlite.py` (grep for its literal expected bytes rather than re-deriving them).

**Acceptance.** A wyrm script that BSON-encodes `{n: "hello", v: 1, g: 2, l: 3}` (the
`hello` header document, Appendix A) produces the same 39 bytes as
`bsonlite.encode_document(...)` on the same input, byte for byte. Round-trip: decoding
the encoder's own output reproduces the original pairs.

**Model.** Sonnet (mechanical port; the contract is fully written down in `bsonlite.py`
and §4 of the format doc).

**Fan-out.** None; single file, sequential encode-then-decode logic that shares helpers.

### M2 — `wy/wyrm/opcodes.wy` generated from `opcodes.py`

**Scope.** New `pypoc/tools/generate_opcode_wy.py`, modeled on
`generate_opcode_header.py`: import `wypoc.compiler_bc.opcodes`, emit a wyrm source file
(`wy/wyrm/opcodes.wy`) holding the same data `c_header()` emits for C, every opcode name,
value, form (core/pairable/long), and for pairable ops both compact and wide values, plus
thin `pack`/`disassemble_one`-equivalent functions if epic 9's image writer needs them (it
needs `pack` at minimum, to encode instruction words when hand-assembling `hello`;
`disassemble_one` can be deferred to epic 10 if nothing in epic 9 exercises it).

**Files.** New: `pypoc/tools/generate_opcode_wy.py`, `wy/wyrm/opcodes.wy` (generated,
checked in). Edit: `pypoc/test/test_compiler_bc_format.py` (or a new
`test_compiler_bc_wy_format.py`) adds a freshness assertion mirroring the existing
opcode.h freshness test: regenerate to a temp file, diff against the checked-in one, fail
loudly if they differ.

**Acceptance.** `pypoc/.venv/bin/python tools/generate_opcode_wy.py` run twice is
idempotent (`already up to date` the second time); the new pypoc test fails if
`opcodes.py`'s `OPS` table changes without regenerating `opcodes.wy`.

**Model.** Sonnet (structurally identical task to the existing C header generator, just a
different target language's syntax).

**Fan-out.** None.

### M3 — `wy/wyrm/image.wy`: section builders, directory, `.wyc` writer

**Scope.** Port `ModuleImage`'s section-building and `.wyc` serialization: a wyrm data
structure holding the pools (statics, symbols, functions, classes, messages, globals) and
a `code` word array, `sections()` producing `{id: bytes}` in ascending id order using
`bjson.wy` for the structured sections and raw code bytes for section 8, and `to_wyc()`
assembling the container: `WYC\0` magic, version byte, section count, 2 reserved bytes,
12-byte directory entries (`id u8, reserved u8, reserved u16, offset u32, length u32`),
4-byte-aligned payloads with zero padding, per `wyc-format.md` §2.

**Files.** New: `wy/wyrm/image.wy` (this milestone only needs `sections()`,
`code_bytes()`, `to_wyc()`; `.wy_a` and `.c` output move to M4).

**Acceptance.** A hand-built `hello` image (statics `["Hello ", "World"]`, one function
`greet` per Appendix A's `functions` entry, the 15 code words from Appendix A, exports
`{greet: 0}`, free `{println: 1}`) serialized with `to_wyc()` matches
`pypoc/test/bytecode/hello.wyc`'s header, directory, and every section byte-for-byte
*except* the debug section (see Assumptions, compare against the `--strip` reference).

**Model.** Sonnet (spec-driven: §2 and §8 of `wyc-format.md` fully specify byte layout).

**Fan-out.** None; directory/offset bookkeeping is sequential and easy to get subtly
wrong split across agents.

### M4 — `.wy_a` and `.c` writers in `image.wy`

**Scope.** Add `to_wya()` (ASCII listing per §5.1, using `opcodes.wy`'s disassembly data
for the code section's comments) and `to_c()` (per §5.2 / `image.py`'s `to_c()`: one
`static const uint8_t <module>_<section>[]` array per non-code section, one
`static const uint32_t <module>_code[]`, and a `const wy_module_image <module>_image`
struct initializer indexed by the `WY_SEC_*` enum). Also port `assemble_wya()` (text back
to `.wyc` bytes) if epic 10's fixture-diffing workflow wants `.wy_a` as a semantic-diff
fallback (per epic 10's milestone policy), confirm that need before committing to it;
if epic 10 turns out to only ever compare `.wyc` bytes and never needs to *reassemble* a
`.wy_a`, `assemble_wya` can be deferred to epic 10 as a YAGNI call, noted in the report.

**Files.** Edit: `wy/wyrm/image.wy`.

**Acceptance.** `to_wya()` on the hand-built `hello` image produces a listing whose
`SECTION` bodies (ignoring comment text, which is not spec-normative) match
`pypoc/test/bytecode/hello.wy_a`'s hex bytes per section. `to_c()` output compiles as
valid C (feed it through the repo's compiler with `-fsyntax-only` against
`include/wyrm/image.h`, once that header exists per epic 1).

**Model.** Sonnet.

**Fan-out.** None (shares state with M3's section builders).

### M5 — hand-assembled `hello`: the exit criterion

**Scope.** `wy/wyrm/tools/build_hello.wy`: a standalone wyrm script that builds the
`hello` `ModuleImage`-equivalent by hand (the statics, the one function, the 15 code
words of Appendix A, encoded via `opcodes.wy`'s `pack`) and writes `to_wyc()`'s bytes to a
file via `bytes`' file-write support (epic 7). Wire it into the exit criterion's two-step
pipeline (pypoc compiles `build_hello.wy` to bytecode, the C VM runs that bytecode, the
program itself writes the target `hello.wyc`-equivalent file).

**Files.** New: `wy/wyrm/tools/build_hello.wy`. New: a `meson test` wrapper (or extend an
existing corpus-runner) that runs the exit-criterion pipeline and `cmp`s the result.

**Acceptance.** The exit criterion command block succeeds; `cmp` reports identical files
(against the stripped reference, per the debug-section decision above).

**Model.** Sonnet.

**Fan-out.** None; this is the integration point, single-threaded by nature.

## Out of scope / deferred

- Emitting a real `debug` section from wyrm (source positions, `ln` table), there is no
  source in this epic's hand-assembly path to derive positions from; epic 10's compiler
  is where debug emission becomes meaningful, and it can revisit whether to match pypoc's
  format exactly or drop the section (a plan-time choice epic 10's executor may revisit).
- `assemble_wya()` (text-to-binary) unless epic 10 concretely needs it for the
  `.wy_a`-semantic-diff fallback described in its milestone policy.
- Any opcode *execution* semantics; `opcodes.wy` in this epic only needs enough to encode
  (`pack`) the 15 `hello` instructions, not a full disassembler.
- `b"..."` byte-string literal syntax (explicitly deferred by epic 7).
- Performance of the bjson/image writer; correctness and byte-identity are the bar.

## Risks

- **`bytes` API gaps.** If epic 7 landed a smaller `bytes` surface than assumed (e.g. no
  `pack_f64`), M1 blocks. Mitigation: the state-scan checklist item 1 catches this before
  milestones start; if a method is missing, add it to `bytes` as a first sub-task of M1
  rather than working around it with string hacks that would defeat the exit criterion's
  determinism.
- **Debug-section mismatch derails the whole exit criterion.** Mitigation: the
  Assumptions section already commits to the `--strip` comparison; do not spend milestone
  time trying to fabricate a matching debug section.
- **`opcodes.py`'s `OPS` table has ~430 rows; a hand-transcribed `wy/wyrm/opcodes.wy`
  would drift immediately.** Mitigation: M2 makes generation mandatory with a freshness
  test, exactly like the existing C header, so no row is ever transcribed by hand.
- **Section id drift.** If the scan finds `image.h`'s `WY_SEC_*` enum and `image.py`'s
  `SECTION_IDS` disagreeing with this file, trust the code, not the plan, and note the
  drift in the report.
- **Alignment/padding off-by-one.** The directory-entry offset math (`directory_end`,
  then `offset = directory_end + len(payloads)`, then pad `payloads` to a multiple of 4)
  is easy to get subtly wrong once ported. Mitigation: M3's acceptance check is a full
  byte diff against a real `.wyc`, which catches any offset error immediately; do not
  accept "looks right" without running `cmp`.

## Report

Write `vm_plan/epic_9_report.md` using the template in `vm_plan/README.md`. Beyond the
template's standard sections, record explicitly:
- The final debug-section decision actually implemented (strip-and-compare, as assumed
  above, or something else the executor chose instead, and why).
- Whether `assemble_wya()` was implemented or deferred, and if deferred, flag it as an
  open item for epic 10's executor to pick up if the `.wy_a` semantic-diff fallback turns
  out to be needed.
- The exact section ids and `WY_SEC_*` names found in `image.h` at execution time, so
  epic 10 does not need to re-derive them.
- Any `bytes` methods that had to be added mid-epic because epic 7 hadn't provided them.
