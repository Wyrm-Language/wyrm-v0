# Epic 7 report — `bytes` type across spec, pypoc, C

Session dates: 2026-09-17 · Models used: Opus (scan, M3's dispatch-mechanism design/implementation,
report), Sonnet subagent (M2, pypoc) · Commits: `de302be`..`65af18e` (main repo),
`cb3c98a`..`17d189c` (pypoc, uncommitted-to-main nested checkout)

## Landed

- **M1** (`de302be`): `bytes` written into `doc/language-spec.md` (Fundamental Types) and
  `doc/stdlib.md` (new `### bytes` section: construction, indexing, all messages,
  pack/unpack semantics, `==`, `str(bytes)` format, `b"..."` deferred). Also re-cut M3 in
  `epic_7.md` for the native-message-dispatch gap found during scan (see below).
- **M2** (pypoc `cb3c98a`; main-repo `wy/std/io.wy` copy `9639c6a`): `BYTES` primitive type
  backed by `bytearray`, all messages via `register_native_method`, `bytes` added to
  `_PRIMITIVE_TYPE_CHECKS`/`TYPE_CHECKS`, `File.read_bytes(size)` in both `io.wy` copies.
  Acceptance: `println(bytes("hi")!to_str())` prints `hi`.
- **M3** (`8abbbbe`): the re-cut scope. Fixed the general native-message dispatch gap
  (`WY_OP_MSG`/`WY_OP_MSG_VA` now fall back to `ctx->builtins`'s own `message_table` and
  invoke a NATIVE-tagged overload body), which also closes epic 5's DIVERGES #1
  (`samples/eval_assignments.wy`, promoted back to `matches`). Implemented the full `bytes`
  method set as native leaf functions registered as PTYPE(BYTES) message overloads:
  construction (bare global `bytes`), `b[i]` get/set, `append`/`resize`/`slice`/`to_str`/
  `copy`, the ten pack/unpack natives, `==` (byte-for-byte, both `wy_op_eq` and
  `vm_ops.c`'s `compare_f`), and `str(bytes)` → `"N bytes"`. Verified byte-identical
  against pypoc via hand-compiled `.wy` scripts (construction, pack/unpack, resize
  zero-padding, slice, copy, `==`, and the range-check fault path).
- **M4** (`15b84ed`): `std::io::write` accepts `bytes` alongside `str`; new
  `std::io::read_bytes(handle, size) -> bytes` native. Verified with a NUL + non-UTF-8-byte
  payload round-tripped through a real temp file via the actual native call path.
- **M5** (pypoc `17d189c`; main repo `65af18e`): the exit criterion. Two `.wy` fixtures
  (`test/bytecode/bytes_header/{pypoc_header,cvm_header}.wy`) hand-build the BSON `header`
  document and write it to a file; `check.sh` runs both plus `bsonlite.encode_document`
  and `cmp`s all three. **All three byte-identical.**

## Deviations from the epic file

- **M3 was re-cut before execution** (recorded in `epic_7.md` itself, 2026-09-17, and in
  the M1 commit message): the scan found `WY_OP_MSG`/`WY_OP_REG_MSG` only ever dispatch to
  a bytecode `FUNCTION` body, which would have made `bytes(3)!pack_u32(0, 258)` — the
  epic's own exit-criterion syntax — fault immediately. This is the same root cause as
  epic 5's DIVERGES #1. Per user decision, M3's scope was expanded to fix this generically
  (a builtins message-table fallback + a NATIVE-body dispatch path) rather than only for
  `bytes`, since `design_c_vm.md` §5 already names this mechanism
  ("`message_table` for per-primitive methods with PTYPE constraints") — an
  implementation gap against an already-written design, not new design work. This also
  retroactively fixes `samples/eval_assignments.wy` (re-wired into `golden`/
  `golden-gcstress`, manifest row promoted `DIVERGES` → `matches`).
- Native leaf functions for `bytes` live in `src/builtin/builtins.c`, not a new
  `src/bytes.c` addition, matching where `list`'s `resize`/`append`/etc. already live —
  `src/bytes.c` keeps only the low-level `wy_bytes_new`/`wy_bytes_reserve` object
  primitives from epic 3, unchanged.
- M4's `std::io::File.read_bytes` (M2, both `.wy` copies) is **pypoc-only**: the C VM
  doesn't load/compile `.wy` corelib sources (no compiler exists in this repo until
  epics 9-11), so the C side got a direct `std::io::read_bytes` native instead, matching
  epic 5/M6's `println` precedent (expose the primitive directly rather than embed a
  compiled wrapper). `write`'s bytes-acceptance is real on both sides.
- M5's two fixture sources are not literally one shared file: pypoc's `std::io` exposes a
  `File` class, the C VM's exposes raw natives on an int handle (M4's own divergence,
  above), so file I/O syntax necessarily differs. The BSON-assembly logic (everything
  before the last three lines) is byte-for-byte identical between the two `.wy` files.
  `check.sh` is a standalone script, not wired into `meson test`/pytest, since the
  fixture's result is a written file, not stdout — it doesn't fit `check_fixture`'s
  stdout-diff contract. The manifest row documents this and defers formalizing
  file-output fixtures to epic 9, per the epic file's own explicit allowance.
- M5 needed one more fix along the way: `File!write` (an instance message) resolves fine
  from *within* `io.wy`'s own module (`write_file` already relies on this) but not
  reliably from an external caller holding the returned `File` — discovered writing the
  exit-criterion fixture. Added `write_bytes_file(path, data)`, a binary-mode top-level
  counterpart to the existing `write_file`, in both `.wy` copies, sidestepping the
  cross-module instance-message call entirely (the same way `write_file` already does for
  text). This is a narrow, targeted addition, not a fix to the underlying cross-module
  message resolution (which is a pypoc interpreter-side issue, outside this epic's scope
  and not investigated further).

## The exact message set landed (both engines)

Construction: `bytes(n: int)`, `bytes(s: str)`, `bytes(b: bytes)` (copy). Indexing: `b[i]`
get/set (int 0-255, range-checked). Messages: `append(int|bytes|str)`, `resize(n)`,
`slice(start, count) -> bytes`, `to_str() -> str` (strict UTF-8, faults on invalid
sequences — matches Python's `bytes.decode("utf-8")` default), `copy() -> bytes`,
`pack_u8/i32/u32/f32/f64(at, v)`, `unpack_u8/i32/u32/f32/f64(at) -> int|float`, all
little-endian and range-checked against `0..len(b)` with no auto-resize. `==` is
byte-for-byte; `is bytes` matches only bytes; `str(bytes)` renders `"N bytes"`. No
deviations from M1's spec text landed in either engine.

## `str(bytes)` rendering format chosen

`"N bytes"` (e.g. `"4 bytes"`), no angle brackets or other punctuation — matches
`pypoc/wypoc/compiler_bc/image.py`'s existing `_static_repr` convention for a binary
constant exactly, per M1's explicit instruction not to invent a second format. (M1's
spec text uses `"<N> bytes"` as placeholder notation for "N substituted here", not
literal angle-bracket characters — worth flagging since it reads ambiguously on its own.)

## Tests

- **pypoc**: before 1520 passed / 1 skipped; after **1548 passed / 1 skipped**
  (`cd pypoc && pytest -q`, confirmed twice — once after M2, once after M5's
  `write_bytes_file` addition). No PR created or pushed from `pypoc/` at any point,
  per `AGENTS.md`.
- **C VM**: before 5/5 Meson tests, 262/262 doctest cases, 16,517 assertions; after
  **5/5 Meson tests (including `cwyrm-golden-gcstress`); 267/267 doctest cases, 16,626
  assertions** (+5 cases / +109 assertions: two `eval_assignments` golden cases
  promoted from unwired-DIVERGES to wired-matches, two new `test_builtins.cpp` cases
  for the native-message-dispatch mechanism and bytes construction/indexing/`==`/
  rendering, one new `test_io_native.cpp` case for the binary-mode I/O round trip).
- **Manifest**: `samples/eval_assignments.wy` moved `DIVERGES` → `matches`; new
  `bytes_header/cvm_header.wy` row added as `matches` (verified via `check.sh`, not the
  golden harness — see above).

## `cmp` result for the exit-criterion round trip

All three byte-identical: pypoc's `bytes_header.bin`, the C VM's `bytes_header.bin`, and
`bsonlite.encode_document({"n": "hello", "v": 1, "g": 2, "l": 3})`'s direct output —
`27000000026e000600000068656c6c6f001076000100000010670002000000106c000300000000`
(39 bytes). Verified via `test/bytecode/bytes_header/check.sh`, which reproduces this
from scratch (compiles both `.wy` sources fresh, runs the reference encoder, `cmp`s all
three) in a scratch directory each run.

## Static-pool `bytes`/`bin` hashability (`image.py:156`)

Never hit. Confirmed by reading every `add_static` call site
(`classes.py`/`functions.py`/`expressions.py`/`module.py`): each only ever passes either a
literal AST node's `constant_value()` (which recognizes `Str`/`Num`/`Char`/`Bool`/
`nil`-`Name` only — never folds a `bytes(...)` call) or a compiler-internal string. A
runtime `bytearray` from evaluating `bytes(...)` never reaches the
`key = (_static_kind(value), value)` dedup tuple. No fix needed.

## Open questions and known gaps

- **`len(bytes)` is unimplemented on both engines** — found while writing M5's fixture
  (`len(b)` in a test script raised `TypeError: len: unsupported value type (bytearray)`
  under pypoc). Worse on the C side: **the C VM has no `len` builtin at all** — no opcode,
  no registered native, confirmed by grepping the whole builtins/opcode surface. No wired
  golden fixture currently calls bare `len(...)` outside decorator-expansion (compile-time)
  code, which is why this was never caught before. Left out of scope for this epic (a
  pre-existing gap, not introduced or widened here); M5's fixture works around it with a
  hand-computed constant since its schema is fixed. Whoever picks up general `len()`
  support should do pypoc's `bytearray` case and the C VM's builtin/opcode in one pass.
- `File!write`'s cross-module instance-message resolution gap in pypoc (found during M5,
  see Deviations) was routed around, not fixed. If it turns out to be a real pypoc
  interpreter bug (versus something specific to how `-c`/REPL-style compilation scopes
  modules), it's worth a closer look — not investigated further here since a workaround
  sufficed for this epic's scope.
- Epic 5's DIVERGES #2-4 (`eval_coroutines.wy`, `eval_closures.wy`, `eval_modules.wy`) are
  untouched — genuinely unrelated to bytes or message dispatch on native bodies.
- No `b"..."` literal syntax, as planned (deferred, one-line rationale in both spec docs).

## Orientation for the next session

- `dispatch_builtins_fallback_f`/`dispatch_native_body_f` (`src/vm.c`, near
  `dispatch_body_f`) are the general native-message-dispatch mechanism; `wy_module_message_lookup_f`
  (`src/message.c`) is the get-without-create lookup they use against `ctx->builtins->message_table`.
  Any future native-message work (a new primitive type's methods, or fixing epic 5's
  remaining DIVERGES entries) should register through `install_native_messages_`
  (`src/builtin/builtins.c`), not invent a second mechanism.
- `bytes` methods are all in `src/builtin/builtins.c` (search `bytes_` prefix), registered
  in both `leaf_builtins_` (construction only, bare global) and `native_messages_`
  (everything else, PTYPE(BYTES)-constrained overloads).
- `compare_f` (`src/vm_ops.c`) now has a BYTES case for `==`/ordering — if a future type
  needs `==` support, check both `wy_op_eq` (`include/wyrm/op.h`, used by `in`/dict keys)
  and `compare_f` (used by the `==`/`<`/etc. opcodes) — they are two separate switch
  statements over the same type tag, easy to update only one by mistake (exactly the bug
  this epic found and fixed for bytes: `wy_op_eq` had a BYTES case from the start, but
  `compare_f` didn't, so `==` silently returned `false` for two content-equal bytes values
  until caught by manual CLI verification, not by the test suite).
- `test/bytecode/bytes_header/check.sh` is the template for any future file-output
  (not stdout) fixture, until epic 9 formalizes something better.
