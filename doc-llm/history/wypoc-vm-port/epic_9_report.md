# Epic 9 report — bjson and image writer in wyrm

Session dates: 2026-09-17 · Models used: Opus (scan, M1–M3), opencode/big-pickle
(M4, report) · Commits: `46cd5e4`..`3ec4c33` (M1–M3 and the `len()` C fix); M4
is in the working tree, not yet committed.

## Landed

- **M1/M2** (`46cd5e4`): `wy/wyrm/bjson.wy` (the 8-tag BSON encoder/decoder over
  `bytes`, `encode_document`/`encode_array`/`encode`/`decode_document`/`decode_array`/
  `decode` plus framing/element helpers) and the generator
  `pypoc/tools/generate_opcode_wy.py` → `wy/wyrm/opcodes.wy`. The freshness test
  `pypoc/test/test_compiler_bc_wy_format.py` asserts the checked-in file still
  equals `generator.opcode_wy()`.
- **M1 findings, fixed** (`34b37e7`, `ba185bc`): `call_va` keyword-argument dicts
  have STR keys, not SYMBOL; `nil == nil` and `symbol == symbol` were always
  false. Both were C-VM bugs surfaced by running wyrm-authored code for the
  first time, not codec bugs.
- **M3** (`c930e6c`): `wy/wyrm/image.wy` section builders and the `.wyc`
  container (`sections()`, `code_bytes()`, `to_wyc()`) plus the milestone driver
  `wy/wyrm/tools/test_image_driver.wy`. Acceptance: the hand-built `hello` image
  is byte-identical to a `--strip` pypoc reference. A follow-on C fix (`3ec4c33`)
  made `len()` a native builtin.
- **M4** (working tree): `to_wya()` / `to_c()` in `wy/wyrm/image.wy`, the
  decoder/disassembler appended to the generated `wy/wyrm/opcodes.wy`, and the
  driver `wy/wyrm/tools/test_image_m4_driver.wy`. Acceptance (all pass):
  `to_wya()` output is byte-identical to the `--strip` pypoc `.wy_a` reference
  (comments included, not just the hex bodies); `to_c()` output is byte-identical
  to the pypoc `hello.c`; `gcc -std=c11 -Wall -Wextra -Werror -Iinclude -c` on the
  generated `.c` succeeds.

## Deviations from the epic file

- **M2 emits the whole decoder/disassembler, not just `pack`.** M4's `.wy_a` code
  comments need disassembly text, and generating it once avoids a second
  regeneration of `opcodes.wy`. `unpack`/`disassemble_one`/`disassemble`/
  `_decode_operand`/`_signed`/`_render_fmt`/`_fmt_return`/`_char_from_cp` live in
  `opcodes.wy` and are image-free; the image-dependent `(note)` hints are split
  into `image.wy`'s `_annotate`/`_describe`, which read each record's
  `fields`/`operands`. This keeps `opcodes.wy` free of an `image` import cycle.
- **`assemble_wya()` deferred to epic 10** (M4's own escape hatch): epic 10
  compares `.wyc` bytes and does not need to reassemble a `.wy_a`, so the
  text→binary direction is YAGNI. Open item if epic 10 wants the `.wy_a`
  semantic-diff fallback.
- **The C VM's builtins message table originally had no `str` methods.** At
  execution time (`src/builtin/builtins.c` `native_messages_[]`) it registered
  only LIST, TABLE and BYTES methods, so `s ! substr(...)` faulted with "no
  overload of 'substr' matches 1 receiver(s)". All string work in this epic
  therefore went through `s[i]` (a codepoint int) plus a local `_char_from_cp`
  that mirrors `decode.wy`'s `_encode_codepoint` (4-case UTF-8 encoder).
  **Closed after M4**: `native_messages_[]` now also registers
  `{ "substr", WY_TYPE_TAG_STR, builtin_substr_, 3, 3 }` (the body already
  existed as a bare C global), so `s ! substr(start, count)` works on the C VM;
  it is codepoint-indexed and clips rather than faulting, matching pypoc's
  `wyrm_builtins.py:696`. `doc/stdlib.md` was corrected (it documented
  `substr(begin, end)`; the real signature is `(start, count)`) and a
  `WY_OP_MSG`-through-the-builtins-fallback doctest now covers it
  (`test_builtins.cpp`, using `"h\xC3\xA9llo" ! substr(1, 3) == "\xC3\xA9ll"`
  so a byte-indexed impl fails). `wy/wyrm/image.wy`'s M4 code keeps using
  `s[i]`/`_char_from_cp` to stay self-contained; it does not depend on the new
  message.
- **`0xFFFFFFFF` literals are rejected by pypoc** ("integer literal 4294967295
  does not fit in an i32"), so `_f32_text` shifts bytes out with `& 0xFF` per
  byte and `_hex8` relies on `_hex_digits`'s `min_digits` padding for i32-valued
  words instead of masking.
- **M5 not landed as a separate script + meson wrapper.** The exit criterion's
  `cmp` is demonstrated by `test_image_driver.wy`/`test_image_m4_driver.wy`
  against the stripped reference, run by hand as documented in their headers.
  Meson wiring was not added: `doc-llm/history/wypoc-vm-port/README.md` requires `meson test` to never
  need Python, and the C VM exposes no `argv` builtin, so a meson-run driver
  could not take its output path from the CLI. Treat the M3/M4 drivers as the
  epic's acceptance artifact.

## Tests

- before: `meson test -C buildDir` 6/6; `pypoc/.venv/bin/pytest` 1551 passed,
  1 skipped; `test_cwyrm` 268 cases / 16679 assertions.
- after: `test_cwyrm` 269 cases / 16687 assertions (the new `str!substr`
  doctest); `meson test` 6/6 and pytest 1551 passed/1 skipped unchanged. M4
  itself touches only `.wy` sources the C build does not compile, so the C
  counts differ from the report's start only by the `substr` test.
- Epic acceptance: `.wy_a` byte-identical, `.c` byte-identical, generated C
  compiles under `-Wall -Wextra -Werror`.

## Open questions and known gaps

- The broader `str`-method gap remains: `native_messages_[]` now has `substr`
  but still no other `str` message. `wy/wyrm/decode.wy` and
  `wy/wyrm/tokenizer.wy` contain `!substr` calls that now resolve; any other
  primitive-message assumption in those files should be checked against the C
  VM before epic 10/11 rely on it.
- `assemble_wya()` is unimplemented (deferred to epic 10).
- No debug section is emitted (`SEC_DEBUG = 9` deliberately absent from
  `image.wy`'s `ALL_SECTION_IDS`); all acceptance uses the `--strip` reference.
  This is the debug-section decision actually implemented.
- `.wy_a` writer assumes `annotations` iterate in ascending offset order (they
  do by construction) rather than re-sorting, unlike pypoc's `sorted(...)`.
- No `bytes` methods had to be added for M4; M1–M3 needed only the epic 7
  surface. (`3ec4c33` fixed `len()` being absent as a native builtin, which
  affects `len(bytes)`/`len(str)`.)

## Facts for epic 10

- Section ids and `WY_SEC_*` names found at execution time (agree with
  `pypoc/wypoc/compiler_bc/image.py` `SECTION_IDS` and `include/wyrm/image.h`):
  `header=1`/`WY_SEC_HEADER`, `statics=2`/`WY_SEC_STATICS`,
  `slot_defaults=3`/`WY_SEC_SLOT_DEFAULTS`, `symbols=4`/`WY_SEC_SYMBOLS`,
  `functions=5`/`WY_SEC_FUNCTIONS`, `classes=6`/`WY_SEC_CLASSES`,
  `messages=7`/`WY_SEC_MESSAGES`, `code=8`/`WY_SEC_CODE`, `debug=9`/`WY_SEC_DEBUG`,
  `exports=10`/`WY_SEC_EXPORTS`, `free=11`/`WY_SEC_FREE`.
- `wy/wyrm/opcodes.wy` exports `pack`, `lookup`, `unpack`, `disassemble`,
  `disassemble_one`, `_decode_operand`, `_char_from_cp`, `_hex2`/`_hex_digits`,
  `reg_name`/`reg_index`/`is_p`/`from_reg8`, `L`/`P`, and the `OPS` table with
  `fmt`/`operands`/`wide_fmt`/`wide_operands`. Underscore-prefixed names ARE
  visible through `import m::*` on the C VM.
- `image.wy` exports `make_image`/`make_param`/`make_function`, `sections`,
  `code_bytes`, `to_wyc`, `to_wya`, `to_c`, and the `_section_annotations`
  machinery. `to_c(image, header_include = "wyrm/image.h")`.

## Proposed edits to epic_10.md

- `substr` is now a real `str` message on the C VM (see Deviations); other
  `str`/primitive messages may still be missing. Before the compiler port
  depends on a primitive message, check `native_messages_[]` or use
  `s[i]` + `_char_from_cp`.
- `opcodes.wy` already carries the full disassembler and `image.wy` the
  `.wy_a`/`.c` writers, so epic 10 can consume them directly; do not re-derive.
- Keep the `--strip` debug-section comparison; epic 10 will be the first place
  with real source positions worth emitting, where the decision may be revisited.

## Orientation for the next session

- Everything in this epic is compiled by `pypoc/.venv/bin/wyrm --build-bc` and
  run on `./buildDir/src/wyrm/wyrm`; never run it on pypoc's tree-walker.
- Run a driver: copy `wy/wyrm/{bjson,opcodes,image}.wy` plus the driver into a
  scratch dir, `--build-bc` each (the C VM resolves imports from `-I` at run
  time and needs the `.wyc` siblings), then
  `./buildDir/src/wyrm/wyrm -I"$work" "$work/<driver>.wyc"`.
- Regenerate the opcode module with
  `pypoc/.venv/bin/python pypoc/tools/generate_opcode_wy.py`; the freshness test
  `pypoc/test/test_compiler_bc_wy_format.py` fails if it drifts.
- `image.wy`'s M4 code deliberately never calls a `str` method (`s[i]` +
  `_char_from_cp` instead) so it does not depend on C-VM message coverage;
  `!substr` now works if a later file prefers it.
