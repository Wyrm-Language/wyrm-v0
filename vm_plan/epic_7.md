# Epic 7 - `bytes` type across spec, pypoc, C

## Goal

Add `bytes` as wyrm's fourth heap-mutable-collection type: a resizable `u8` array with
message-based get/set, append/resize/slice, string conversion, and little-endian
pack/unpack for the four numeric widths wyrm cares about. Land it in the spec, in the
pypoc tree-walker, and in the C VM, so a wyrm script can build and write binary data that
neither engine could produce before this epic (pypoc is currently text-only I/O; the C VM
has the `wy_bytes` object shape reserved in `design_c_vm.md` §4 but no methods). This is
the prerequisite for epic 9's bjson writer.

**Exit criterion:**
```
pypoc/.venv/bin/wyrm -Iwy test/bytecode/bytes_header.wy
./buildDir/src/wyrm/wyrm test/bytecode/bytes_header.wyc
```
Both runs write a file byte-identical to
`python3 -c "from wypoc.compiler_bc import bsonlite; import sys; sys.stdout.buffer.write(bsonlite.encode_document({'n': 'hello', 'v': 1, 'g': 2, 'l': 3}))"`.
`cmp` all three outputs; they must match exactly.

## Inputs

- Previous report: none yet. Epic 7 can start after epic 3 (C-side `wy_bytes` object
  minimum) and epic 1 (pypoc side). If `vm_plan/epic_6_report.md` or a later report
  exists, read its "Orientation for the next session" for anything amending
  `design_c_vm.md` §4/§5. If no reports exist yet, proceed from `design_c_vm.md` as
  written and note in this epic's report that it ran ahead of the C-side epics.
- State-scan checklist:
  1. Does `include/wyrm/bytes.h` exist, and does `struct wy_bytes` match §4's
     `{ wy_object object; wy_uword len, capacity; wy_u8* data; }`?
  2. Is `WY_TYPE_TAG_BYTES` already in the type tag enum?
  3. Does a builtins module source exist (`src/builtin/builtins.c` per §11's file list),
     and what does its native-registration mechanism look like?
  4. Is `pypoc/wypoc/wyrm_builtins.py`'s `PrimitiveType`/`register_native_method` shape
     still as described (lines ~72-96, ~788-859)?
  5. Does `pypoc/wypoc/wyrm_io.py` still open files via Python's own `open()` with a
     passed-through mode string, so `"rb"`/`"wb"` already work?
  6. Does `pypoc/wypoc/compiler_bc/image.py`'s `_static_kind`/`_static_repr` still return
     `"bin"` for `bytes`/`bytearray` (~728-758), and is that path actually exercised by
     any test?
  7. Grep `pypoc/wypoc/corelib/std/io.wy` and `wy/std/io.wy` for any existing `bytes(...)`
     call; expect none (both `File.read`/`File.write` are typed `-> str` today).
  8. Run `cd pypoc && pytest -q`, record the pass count before touching pypoc.
  9. Run `meson test -C buildDir`, record the pass count before touching C.
  10. Confirm nothing already claims the name `bytes` as a builtin binding in either
      engine (would be a silent shadow, not an error).

## Context to load

Read in full:
- `vm_plan/design_c_vm.md` §4 (`wy_bytes` struct, value tags), ~1k tokens.
- `vm_plan/design_c_vm.md` §5, the "Builtins are a module" paragraph, ~300 tokens.
- `pypoc/doc/wyc-format.md` §4 "The BSON subset" and §8 "Section schemas" (`statics`),
  ~1.5k tokens.
- `pypoc/wypoc/compiler_bc/bsonlite.py` in full (223 lines): `_element`, `_value`'s
  int32/double/string/binary branches, `_cstring`, `_frame`. This is the exact byte
  layout the exit criterion checks against.
- `doc/language-spec.md` around line 885-920 ("Types and Type System"), the one-line
  `bytes: a byte buffer` entry this epic replaces.

Grep-only, pull specific ranges:
- `pypoc/wypoc/wyrm_builtins.py`: `PrimitiveType` (~72-86), `PRIMITIVE_TYPES` (~201-206),
  `install()` (~788-859), an existing `register_native_method` call (e.g. `resize`) as a
  template.
- `pypoc/wypoc/wyrm_eval_parse_tree.py`: `_PRIMITIVE_TYPE_CHECKS` (~3246-3261, add
  `"bytes"`), `_matches_type` (~3263-3275).
- `pypoc/wypoc/wyrm_io.py`: `wyrm_open`/`wyrm_read`/`wyrm_write` (~40-70).
- `pypoc/wypoc/corelib/std/io.wy` and `wy/std/io.wy`: `File` class, `open`/`read_file`/
  `write_file`.
- `pypoc/wypoc/compiler_bc/image.py`: `_static_kind`/`_static_repr` (~728-758) and the
  `key = (_static_kind(value), value)` dedup line (~156, `bytearray` is unhashable).
- `pypoc/test/test_vm_samples.py`: `TYPE_CHECKS` tuple (~142-146).
- `pypoc/AGENTS.md`: the pytest-green rule and the no-PR rule.
- C side (confirm paths via scan first): `include/wyrm/bytes.h`, the builtins module
  source, the native leaf function signature in §1.3, and whatever hosted `std::io` port
  epic 5/6 produced.

Rough budget: 6-8k tokens loaded.

## Assumptions

- `struct wy_bytes` is already declared per §4 with no methods wired *(verify in scan)*.
- `WY_TYPE_TAG_BYTES` exists in the tag enum *(verify in scan)*.
- pypoc represents `bytes` as a Python `bytearray` (mutable, mirrors how `list` uses a
  native Python `list`) *(verify in scan against `resize`/`expand`/`append` for `list`)*.
- `wyrm_io`'s open/read/write need no Python-level change; `open(path, "rb")` already
  returns/accepts `bytes` *(verify in scan)*.
- The `.wy`-level `File` class is the only piece needing new methods, since it hardcodes
  `-> str` today *(verify in scan)*.
- The C builtins registration mechanism is a table this epic can append to, not something
  requiring a new dispatch mechanism *(verify in scan)*.
- No `b"..."` literal syntax is in scope; `bytes(str)` covers construction from source
  text *(fixed by plan, not to be revisited without discussion)*.
- Static-pool `bytes` constants (BSON `statics`, binary subtype 0) are already correctly
  read/written by both engines' existing static-pool machinery; this epic adds only
  *runtime* `bytes` behavior *(verify in scan - confirm an existing test exercises a
  binary static, or add one in M1)*.

## Milestones

### M1 - Spec text

**Scope:** Write `bytes` into `doc/language-spec.md` and `doc/stdlib.md`: heap, mutable,
resizable `u8` array; `bytes(n)` (n zero bytes), `bytes(str)` (UTF-8 encode); `len(b)`;
`b[i]` get returns int 0-255, set range-checks; `append(int|bytes|str)`, `resize(n)`,
`slice(start, count) -> bytes` (copy), `to_str() -> str` (UTF-8 decode; state the fault
behavior on invalid sequences explicitly), `pack_u8/i32/u32/f32/f64(at, v)` and matching
`unpack_*(at)`, all little-endian, `copy`, `==` (byte-for-byte), `is bytes`, `str(bytes)`
rendering (pick a concrete format; `image.py:758`'s `f"{len(value)} bytes"` is a
reasonable model if adopted, say so explicitly). Note `b"..."` literal as deferred with a
one-line rationale.

**Files:** `doc/language-spec.md`, `doc/stdlib.md` (new `### bytes` section mirroring the
existing `### str` section).

**Acceptance:** every message above is listed with signature and one line of semantics; a
reader can predict `bytes(3)!pack_u32(0, 258)`'s resulting bytes from the text alone.

**Model:** Sonnet, spec-driven and mechanical once the message set is fixed.

**Fan-out:** none.

### M2 - pypoc: `bytes` builtin type and methods

**Scope:** Add `BYTES = PrimitiveType("bytes", _to_bytes)` next to `STR`/`INT`/...,
backed by `bytearray`. `_to_bytes` stercasts int to n zero bytes, str to UTF-8 encode,
bytes/bytearray to a copy. Regi native methods `append`, `resize`, `slice`, `to_str`,
`pack_u8/i32/u32/f32/f64`, `unpack_u8/i32/u32/f32/f64`, `copy` via
`register_native_method`, following the existing `resize`/`append` pattern for `list`.
Wire `b[i]` get/set through the existing indexing dispatch. Add `"bytes"` to
`_PRIMITIVE_TYPE_CHECKS` and `("bytes", "bytes(0)")` to `TYPE_CHECKS`. Use Python's
`struct` module for pack/unpack, matching `bsonlite.py`'s own `struct.pack("<i", ...)`/
`struct.pack("<d", ...)` calls so the two stay in sync by construction. Add binary-mode
support to `File` in both `pypoc/wypoc/corelib/std/io.wy` and `wy/std/io.wy`: `write`
accepts either `str` or `bytes` and passes through to `__write`; add `read_bytes(size)`
for a handle opened `"rb"`.

**Files:** `pypoc/wypoc/wyrm_builtins.py`, `pypoc/wypoc/wyrm_eval_parse_tree.py`,
`pypoc/wypoc/corelib/std/io.wy`, `wy/std/io.wy`, `pypoc/test/test_vm_samples.py`, a new
`pypoc/test/test_bytes.py` (check for an existing builtins test file first) covering each
message including a pack/unpack round trip per width and a `bsonlite`-equivalence
assertion.

**Acceptance:** `cd pypoc && pytest -q` stays fully green including the new tests
(pypoc's own AGENTS.md rule: pytest green, never create PRs from this tree);
`wyrm -Iwy -c 'println(bytes("hi")!to_str())'` prints `hi`.

**Model:** Sonnet, mechanical port of an established `struct`-based encoding against an
existing reference.

**Fan-out:** up to 2 Sonnet subagents ("pack/unpack natives" vs. "File binary mode") on
disjoint files if the milestone runs long; otherwise sequential.

### M3 - C: `wy_bytes` methods, builtins registration, and native-message dispatch

**Re-cut 2026-09-17 (scan finding, before execution):** epic 7's own exit criterion calls
`bytes` methods via `!`-message syntax (`bytes(3)!pack_u32(0, 258)`), but `WY_OP_MSG`
(`src/vm.c` ~1607) and `WY_OP_REG_MSG` (~1797) only ever handle a bytecode `wy_function`
body — `dispatch_body_f`'s result is cast unconditionally to `wy_function*` and handed to
`push_bytecode_call_bind_f`, and `reg_msg` faults `"reg_msg: not a function"` on anything
that isn't `WY_TYPE_TAG_FUNCTION`. This is the same root cause epic 5's report recorded as
**DIVERGES #1** (`samples/eval_assignments.wy`'s `grown!resize(5)` faults "no overload of
'resize' matches 1 receiver(s)"), confirmed still unfixed by this scan. Per user decision
2026-09-17: M3 now includes fixing this generically rather than only for `bytes`, since
`design_c_vm.md` §5 already anticipates it ("Builtins are a module... `message_table` for
per-primitive methods with PTYPE constraints") — this is an implementation gap against an
already-written design, not a new design task.

**Scope:**
1. **Native-message dispatch** (new sub-milestone, do first): give the builtins module a
   `message_table` (per §5) holding PTYPE-constrained overloads whose body is a native
   leaf, reachable from *any* module's `WY_OP_MSG` resolution (not just the receiver's own
   module) — `wy_module_resolve_message_f` needs a fallback to `ctx->builtins->message_table`
   when the receiver's own module has no matching entry. Add a body-type branch at both
   `WY_OP_MSG`'s dispatch call site and `WY_OP_MSG`'s super/interface-dispatch twin (~1689)
   so a NATIVE-tagged body invokes the existing native leaf-call path
   (`wy_vm_call_leaf_f`/equivalent) instead of `push_bytecode_call_bind_f`. `WY_OP_REG_MSG`
   itself stays FUNCTION-only (bytecode-authored messages); native overloads are registered
   directly from C at builtins-init time, not through `reg_msg`. Add a doctest exercising
   `grown!resize(5)` end-to-end (epic 5's own DIVERGES #1 fixture) to confirm the general
   fix, in addition to `bytes`.
2. Implement M1's `bytes` method set as native leaf functions (`wy_native_leaf_fn` from
   §1.3), registered as PTYPE(BYTES)-constrained overloads via (1)'s mechanism.
   `bytes(n)`/`bytes(str)` construction, `len`, `b[i]` get/set via the `getidx`/`setidx`
   dispatch epic 3 built (add a `BYTES` case), `append`/`resize`/`slice`/`to_str`/`copy`/
   `==`/`is bytes`, and the eight pack/unpack natives using explicit little-endian byte
   writes (never assume host endianness). Grow `wy_bytes.data` via `wy_allocator` only, per
   `AGENTS.md`.

**Files:** `include/wyrm/bytes.h`, `src/bytes.c` (new, or fill in an epic-2/3 stub),
`src/vm.c` (`WY_OP_MSG` body-type branch), `src/message.c`/`src/module.c` (builtins
message-table fallback resolution), the builtins module source, doctest cases in the
per-type test file epic 3/4 established, plus one covering the general fix via
`grown!resize(5)`.

**Acceptance:** a doctest case constructs `bytes(4)`, calls `!pack_u32(0, 0xdeadbeef)` via
the real `!`-message opcode path (not a direct native call), and asserts the bytes read
`EF BE AD DE`; a second doctest confirms `samples/eval_assignments.wy`'s previously-faulting
`grown!resize(5)` now runs; `meson test -C buildDir` green. If `eval_assignments.wy` fully
passes end-to-end, promote its manifest row from `DIVERGES` back to `matches` and wire it
into the golden harness.

**Model:** Sonnet for part 2 (byte order and contracts fully specified by M1/M2, a port not
a design). Part 1 (native-message dispatch) is implementation against an existing design
section, not first-of-kind design work, so Sonnet is appropriate, but flag to Opus if the
builtins-fallback resolution turns up a design question §5's text doesn't answer (e.g.
ambiguity between a module's own overload and a builtins one at the same arity).

**Fan-out:** part 1 first, sequential (touches shared dispatch code, easy to conflict);
then up to 2 Sonnet subagents in parallel for part 2: one for construction/len/indexing/
append/resize/slice/copy/`==`, one for the eight pack/unpack natives, pre-agreeing on a
registration-table naming convention to avoid a merge conflict.

### M4 - `std::io` binary mode on the hosted port

**Scope:** Whatever epic 5/6 built for `std::io` needs a binary-mode open path
returning/accepting `wy_bytes` instead of `wy_string`, mirroring M2's `File` change. If
hosted `std::io` hasn't landed yet (check the latest epic report), scope this down to
whatever native open/read/write natives exist at scan time, or mark it deferred to the
epic that lands hosted I/O, with a one-line note in this epic's report. Also check whether
the C VM loads `.wy` corelib sources at all yet, or only pre-compiled `.wyc`; if the
latter, this milestone is pypoc-only until epics 9-11 land, and the report should say so.

**Files:** hosted platform I/O source (path TBD by scan, likely `src/platform/hosted/`),
plus the corelib `File` class if the C toolchain reads `.wy` corelib sources.

**Acceptance:** if in scope, a C-side test opens a file in binary mode, writes a
`wy_bytes` value, reads it back, and the bytes match. If descoped, the report states why.

**Model:** Sonnet by default; escalate to Opus only if the hosted I/O layer doesn't exist
yet and a design decision is genuinely needed.

**Fan-out:** none, depends on M3.

### M5 - Exit criterion: the BSON header round trip

**Scope:** Write `test/bytecode/bytes_header.wy` (or wherever epic 1's corpus script
reads `.wy` fixtures from): build the BSON `header` document `{ n: "hello", v: 1, g: 2,
l: 3 }` by hand using `bytes` methods, matching `bsonlite.py`'s `_frame`/`_element`/
`_value`/`_cstring` exactly, and write it to a file. Run under pypoc, and under the C VM
if M4 landed. Compare all outputs byte-for-byte against `bsonlite.encode_document` via
`cmp`. Add as a corpus regression fixture if epic 1's `scripts/build_corpus.py` machinery
exists; otherwise a standalone script is acceptable, with a note for epic 9 to formalize.

**Files:** the new `.wy` fixture, a comparison script, a `test/bytecode/manifest.txt`
entry if the corpus mechanism exists.

**Acceptance:** the exit criterion command block at the top of this file passes; `cmp`
reports no differences among the outputs actually runnable.

**Model:** Sonnet, assembling known primitives against a known reference format.

**Fan-out:** none, single integration point, done last.

## Out of scope / deferred

- `b"..."` literal syntax; `bytes(str)` covers construction from source text.
- Any change to BSON *section-loading* machinery; this epic is runtime `bytes` behavior.
- Big-endian host support beyond not assuming host endianness in the byte-write code; no
  big-endian CI target exists to verify against.
- A general `std::io` binary-mode redesign; M4 targets only the exit criterion's path.
- `bytes` growth performance (amortized doubling etc.); correctness first.

## Risks

- C-side epics 1-6 may not have landed a builtins module in the assumed shape. Mitigation:
  scan items 1-3 catch this first; re-cut M3's file list before starting if it differs,
  and record the drift in the report.
- pypoc's `bytearray` choice is unhashable, which matters if `bytes` ever reaches
  `image.py:156`'s `key = (_static_kind(value), value)` dedup tuple. Mitigation: scan item
  6 checks whether this path is actually exercised for runtime `bytes` (it shouldn't be,
  since `bytes(...)` calls are never constant-folded into statics); if it matters, use
  immutable `bytes` for the static-pool representation and `bytearray` only for the
  mutable runtime value, matching `str` vs. `list`.
- Endianness bugs in the C pack/unpack natives are easy to miss on a little-endian
  machine. Mitigation: M3's acceptance test asserts exact byte values, not just round-trip
  equality.
- `to_str()`'s fault behavior on invalid UTF-8 is not fully specified by the plan.
  Mitigation: M1 must pick a concrete behavior and M2/M3 must match it; Python's own
  `bytes.decode("utf-8")` (which raises) is a natural default if nothing forces otherwise.
- The exit criterion's C-side half may be blocked on epic 5/6's hosted `std::io` not
  existing yet. Mitigation: M4 explicitly allows descoping to pypoc-only with a documented
  reason; the second exit command is then a known gap in the report, not a blocker.

## Report

`vm_plan/epic_7_report.md` follows the README template, plus:

- The exact message set landed for `bytes` in both engines, with deviations from M1.
- Whether M4 (C-side `std::io` binary mode) landed or was descoped, and which later epic
  should pick it up if descoped.
- The chosen `str(bytes)` rendering format.
- Confirmation that `cd pypoc && pytest -q` is green and no PR was created there, with
  before/after test counts.
- The `cmp` result for the exit-criterion round trip, across however many engines were
  actually runnable.
- Any static-pool `bytes`/`bin` hashability issue found at `image.py:156` and its
  resolution, or confirmation it was never hit.
