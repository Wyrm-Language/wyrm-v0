# Epic 1 — Toolchain, conformance corpus, image loader

## Goal

Make the C tree able to *read* every module image pypoc can produce, and give every later
epic a committed, Python-free conformance corpus to run against. This epic establishes the
pypoc toolchain locally, generates and commits `.wyc` + expected-output fixtures, adopts
pypoc's `opcode.h` and `image.h` verbatim, and replaces the current `src/module.c` loader
(which parses a `WYC\x02` BSON-document container that pypoc never emits) with a loader for
the normative `WYC\0` + directory format in `pypoc/doc/wyc-format.md`, populating every
section into `wy_module` tables with load-time bounds checks. No instruction executes yet.

**Exit criterion:**
```
meson test -C buildDir --suite loader   # every test/bytecode/**/*.wyc loads; reject cases pass
./buildDir/src/wyrm/wyrm test/bytecode/hello.wyc --sections
# prints: header n=hello v=1 g=2 l=3 | statics 2 | functions 1 | code 15 words | exports 1 | free 1
```

## Inputs

No previous report. State-scan checklist (run with Explore subagents; record findings at
the top of `epic_1_report.md`):

1. `meson setup buildDir && meson compile -C buildDir && meson test -C buildDir` is green
   and record the case count (was 75 cases / 453 assertions on 2026-09-15).
2. `git status` is clean apart from `pypoc/` being ignored; the uncommitted
   `include/wyrm/context.h` change (`WY_CONTEXT_MODULE_INITIAL`) and `doc/EXPLAINER.md`
   have been committed or are committed as this epic's first commit.
3. `pypoc/.venv` exists, or create it: `cd pypoc && python -m venv .venv && .venv/bin/pip
   install -e ".[dev]"`; then `.venv/bin/pytest -q` is green (record count).
4. `pypoc/wypoc/compiler_bc/include/wyrm/opcode.h` still says `WYRM_OP_COUNT 88` and
   `image.h` still has `WY_SEC_COUNT` with ids 1..11; `pypoc/doc/wyc-format.md` §2 magic is
   `57 59 43 00`, version 1.
5. `src/module.c:34-104` still checks magic `'W','Y','C',0x02` and iterates named BSON
   fields (i.e. nothing has started on the loader).
6. `include/wyrm/module.h` `wy_module` is still `{globals, code}` only.
7. `src/bson.c:82` still has the `(pos + 2) >= data_len` early-stop that treats truncation
   as end-of-document.
8. `.gitignore` ignores `*.wyc` globally (it does); this epic adds an exception.
9. `pypoc/test/bytecode/` contains: hello, hello_1/2/3, arith, classes, closures,
   collections, control_flow, coroutines, errors, messages, multiret, two_module/,
   wildcard/, decorators/; three `*.vm.out` files exist (arith, collections, multiret).
10. `pypoc/test/test_vm_samples.py` REFUSED has 10 entries and DIVERGES has 1
    (`eval_messages.wy`).

## Context to load

Read (≈25k tokens total):
- `vm_plan/README.md`, `vm_plan/design_c_vm.md` §0, §5, §9 (loader, module struct, testing).
- `AGENTS.md` (all), `doc/EXPLAINER.md` (all).
- `pypoc/doc/wyc-format.md` §2 container, §3 sections, §4 BSON subset, §7 load sequence,
  §8 section schemas, Appendix A and B. Skip §5–§6 (instruction set) for this epic.
- `pypoc/wypoc/compiler_bc/include/wyrm/image.h` (37 lines), `opcode.h` (130 lines).
- `include/wyrm/module.h`, `src/module.c` (214 lines), `include/wyrm/bson.h`,
  `src/bson.c` (144 lines), `include/wyrm/mem_info.h`, `include/wyrm/slot.h` + `src/slot.c`
  (the symbol→index hash to reuse for exports/free), `include/wyrm/context.h:101-121`
  region of `src/context.c` (module registry).
- `src/meson.build`, `src/test/meson.build`, `src/test/test_bson.cpp`,
  `src/test/test_common/test_context_fixture.h`.
- `src/wyrm/main.c` (132 lines; becomes the CLI).
- `pypoc/test/test_vm_load.py` (308 lines): every rejection rule the C loader must mirror.
- `pypoc/test/conftest.py:98-145` (fixture helpers), `pypoc/test/test_vm_samples.py:38-61`
  (REFUSED/DIVERGES), `pypoc/test/test_vm_run.py:108-155` (RUNNABLE, DIVERGENCES).
- `pypoc/wypoc/cli.py:294-342` (`--build-bc`, `--emit`, `--strip`, `-o`).

Grep only:
- `pypoc/wypoc/vm/image.py` (`read_wyc`, `_check_directory`) to confirm rejection order.
- `pypoc/wypoc/compiler_bc/image.py:470-550` (`to_wyc`, `.c` writer) for byte layout.
- `include/wyrm/sys/errors.h` for existing `wy_error` codes; add `WY_ERR_IMAGE`,
  `WY_ERR_LINK` if absent.

## Assumptions

- The existing `wy_bson_doc_reader` (zero-copy, `data/data_len/pos`) is worth keeping as the
  reader core; only strictness changes (8 permitted tags, subtype 0, truncation error)
  *(verify in scan)*.
- `wy_slot_dict` in `src/slot.c` is a working open-addressed symbol→u16 hash usable for the
  `exports` and `free` maps *(verify in scan: it has tests in `test_slot.cpp`)*.
- Symbols can be interned for now through the existing scaffold symtab in `src/machine.c`
  (8 KB, linear); epic 2 replaces it. Loader code must call a single `wy_context_intern`
  entry point so the swap is one function *(verify in scan)*.
- pypoc's `--build-bc` module name is the file stem; multi-module fixtures need the search
  root set to their directory (`conftest.py`) *(verify in scan)*.
- The tree walker's stdout is the expected output for every runnable fixture except the
  three with checked-in `*.vm.out` (multi-value divergence), which are VM-correct
  *(verify in scan)*.
- `hello_1.c`-style embedded images compile against `image.h` alone; the checked-in
  `pypoc/test/bytecode/*.c` are stale (retired opcodes) and must be regenerated, never
  copied *(verify in scan)*.
- meson can pass `-DWY_TEST_BYTECODE_DIR="…"` to the test executable
  *(verify in scan: `src/test/meson.build`)*.

## Milestones

### M1 — pypoc toolchain and corpus generator
**Scope**
- `scripts/setup_pypoc.sh`: creates `pypoc/.venv`, installs `.[dev]`, runs
  `pytest -q`, prints the `wyrm` path. Idempotent.
- `scripts/build_corpus.py` (Python, runs with `pypoc/.venv/bin/python`): for each
  `pypoc/test/bytecode/**/*.wy` and each `pypoc/wypoc/samples/*.wy`:
  - compile with `compile_module` (as `test_vm_samples.py:compile_sample` does; catch
    `CompileError` → REFUSED with the message);
  - write `test/bytecode/<rel>/<name>.wyc` and `.wy_a` (`--strip` off: keep `debug`; the
    C loader must ignore it anyway);
  - expected output: run the tree walker (`test_vm_samples.py:under_the_walker`) with
    the search root set to the fixture's directory; for names in `test_vm_run.py`'s
    `DIVERGENCES`, copy the checked-in `.vm.out` instead; for `DIVERGES` samples write
    the walker output but mark the manifest row DIVERGES with the reason;
  - write `test/bytecode/manifest.txt`, one row per source:
    `name<TAB>wyc-path<TAB>out-path<TAB>status<TAB>reason` with status in
    {matches, REFUSED, DIVERGES, fragment}. "fragment" = the sample needs harness-supplied
    names (see `REFUSED` reasons in `test_vm_samples.py`); it is compiled if the compiler
    accepts it but never run.
  - regenerate `test/bytecode/embedded/hello_1.c` (and `hello_2`, `hello_3`) with
    `--emit c`.
- `.gitignore`: add `!test/bytecode/**/*.wyc` after the `*.wyc` line.
- Commit the corpus. Document the regeneration command in `doc/vm_impl.md`.

**Files** new: `scripts/setup_pypoc.sh`, `scripts/build_corpus.py`, `test/bytecode/**`;
changed: `.gitignore`, `doc/vm_impl.md`.

**Acceptance**
```
scripts/setup_pypoc.sh && pypoc/.venv/bin/python scripts/build_corpus.py
git status --short test/bytecode | wc -l    # > 40 files
grep -c matches test/bytecode/manifest.txt   # ≥ 19 (RUNNABLE fixtures) + ≥ 11 samples
```
**Model** Sonnet (mechanical; the pypoc test files already show every call).
**Fan-out** none.

### M2 — Adopt `opcode.h` and `image.h`; delete the old encoding
**Scope**
- `scripts/sync_pypoc_headers.py`: copies the two headers from
  `pypoc/wypoc/compiler_bc/include/wyrm/` into `include/wyrm/`, adding a one-line
  provenance comment. Idempotent.
- Remove the old `include/wyrm/opcode.h` contents (`WY_OP_PASS/LBYTE/SET`,
  `wy_opcode_pack`, `wy_opcode_get_flag` with `f` at bits 24-31, `WY_OP_LONG_START = 127`)
  and `wy_vm_exec_bytecode` in `src/vm.c`; keep `src/vm.c` compiling as a stub that
  returns `WY_ERR_INVAL` (epic 2 rewrites it). Fix `src/test/test_wvm.cpp` accordingly
  (delete cases that packed the old encoding; keep the module-id/address packing test).
- `src/test/test_headers_sync.cpp`: a doctest that reads both header files at test time
  (paths via `-D`) and fails if they differ from the pypoc copies; skipped with a message
  when `pypoc/` is absent.
- Generate `include/wyrm/opcode_names.h` (a `static const char* const wy_opcode_names[256]`
  table) from `pypoc/wypoc/compiler_bc/opcodes.py` via a new
  `pypoc/tools/generate_opcode_names.py`; commit the output; the sync script runs it.

**Files** new: `scripts/sync_pypoc_headers.py`, `pypoc/tools/generate_opcode_names.py`,
`include/wyrm/opcode_names.h`, `src/test/test_headers_sync.cpp`; changed:
`include/wyrm/opcode.h`, `include/wyrm/image.h` (new), `src/vm.c`,
`src/test/test_wvm.cpp`, `src/test/meson.build`.

**Acceptance**
```
scripts/sync_pypoc_headers.py && git diff --stat include/wyrm   # no diff after first run
meson compile -C buildDir && meson test -C buildDir              # green
```
**Model** Sonnet.
**Fan-out** none.

### M3 — Container and strict BSON reader
**Scope**
- `include/wyrm/image.h` (adopted) + new `src/image.c`:
  `wy_error wy_image_from_bytes(const wy_u8* data, wy_uword len, wy_module_image* out)`
  implementing every rule in wyc-format.md §2 and Appendix B step 1: magic, version 1,
  section count, directory sorted ascending with unique ids, `offset + length` within
  `len`, unknown id → `WY_ERR_IMAGE` except id 9 (debug) which is recorded and ignored,
  4-byte alignment of each payload (reject if not; the writer guarantees it), `code`
  payload length multiple of 4, required sections 1 and 8 present. Zero copies.
- `src/bson.c` strictness: accept only tags 0x01, 0x02, 0x03, 0x04, 0x05 (subtype 0),
  0x08 (value 0/1), 0x0A, 0x10; any other tag → `WY_ERR_IMAGE`. Fix the truncation
  ambiguity at `src/bson.c:82`: end-of-document is *only* the 0x00 terminator at
  `pos == data_len - 1` with the declared length matching; anything else is
  `WY_ERR_IMAGE`. Add `wy_bson_array_reader` convenience (documents with index keys;
  readers may ignore keys) and `wy_bson_get_double_f`.
- Tests: port every case in `pypoc/test/test_vm_load.py` that concerns the container or
  BSON (bad magic, bad version, unsorted directory, duplicate id, unknown id, out-of-range
  offset, truncated payload, non-permitted tag, bad bool byte, binary subtype ≠ 0) using
  byte-array fixtures built in the test from `hello.wyc` with single-byte mutations.

**Files** new: `src/image.c`, `src/test/test_image.cpp`; changed: `src/bson.c`,
`include/wyrm/bson.h`, `src/test/test_bson.cpp`, `include/wyrm/sys/errors.h`
(`WY_ERR_IMAGE`, `WY_ERR_LINK`), `src/meson.build`.

**Acceptance** `./buildDir/src/test/test_cwyrm --test-case="*image*" --test-case="*bson*"`
green, including at least 10 rejection cases.
**Model** Sonnet (spec is explicit).
**Fan-out** allowed: one subagent on `src/image.c` + `test_image.cpp`, one on `src/bson.c`
strictness + `test_bson.cpp`; disjoint files; both use `hello.wyc` bytes.

### M4 — Module tables: load every section
**Scope**
- Rewrite `include/wyrm/module.h` to the `wy_module` in design_c_vm.md §5 (name, state,
  image bytes + ownership, `code`/`code_len`/`init_nlocals`, `globals` (all Unset),
  `statics`, `symbols`, `functions` (`wy_function_proto`), `class_protos`, `messages`
  (`wy_message_ref` with `bound = NULL`), `exports`, `free_names`, `fill_layer` /
  `fill_source` arrays, `wildcards` empty). Leave `message_table` and `classes[]` as
  NULL until epics 4–5 but declare them.
- `src/module.c`: `wy_module_load_image(ctx, image, &module)` performing load steps 1–5
  of §7.1 except builtin filling (epic 2): parse header (`n`, `v == 1`, `g`, `l`;
  ignore `u`), allocate globals Unset, apply `slot_defaults`, intern `symbols`
  (31-codepoint prefix rule noted; epic 2 enforces it), read `statics` into `wy_value`
  (string → `wy_string`, int32 → WORD, double → keep as double bits in a temporary
  representation until epic 2 adds FLOAT; plan-time choice: store doubles as a
  `WY_TYPE_TAG_ERROR`-free placeholder `WY_TYPE_TAG_DTYPE`? No: add `WY_TYPE_TAG_FLOAT`
  now, minimal, with `data.fp`; epic 2 fills in arithmetic), bool, null, binary
  (placeholder object until `wy_bytes` exists in epic 2: store as `wy_string` with a
  flag; record this in the report), `functions` (all keys; `t` as global slot indices,
  `k`, `r` defaults), `classes` (all keys; superclass is a global slot index), `messages`
  (paths as symbol indices), `exports` and `free` into `wy_slot_dict`s, `code` pointer in
  place. Bounds-check every index against its table (§3 "reject any index out of range")
  and every function `c` against `code_len`; class message map ≤ 16.
- `wy_module_load_bytes(ctx, data, len, take_ownership, &module)` =
  `wy_image_from_bytes` + `wy_module_load_image`.
- Remove `wy_module_code_push`/`wy_module_reserve_code_f` and the `WYC\x02` path.
- GC: extend `children_iter` to walk `globals` and `statics`.
- `test/bytecode/embedded/hello_1.c` compiled into `test_cwyrm`; a test loads
  `hello_1_image` through `wy_module_load_image`.
- Tests: for `hello.wyc` assert `g == 2`, `l == 3`, statics `["Hello ", "World"]`,
  `functions[0]` = `{n: greet, nparams 1, nlocals 2, code_offset 11, flags 0}`, exports
  `{greet: 0}`, free `{println: 1}`; for `classes.wyc` assert slot layout and message
  map counts; for `two_module/report.wyc` assert the `messages` and `free` tables; a
  suite `loader` that loads every `.wyc` listed in `manifest.txt` (from
  `WY_TEST_BYTECODE_DIR`) and expects `WY_ERR_NONE`.

**Files** changed: `include/wyrm/module.h`, `src/module.c`, `src/gc.c` (if needed),
`include/wyrm/primitive.h` (`FLOAT` tag), `src/test/test_module.cpp` (new),
`src/test/meson.build` (`WY_TEST_BYTECODE_DIR`, compile `hello_1.c`).

**Acceptance** `meson test -C buildDir --suite loader` green; `test_cwyrm
--test-case="*module*"` green.
**Model** Opus for the `wy_module` header and the first section (sets the pattern);
Sonnet for the remaining sections.
**Fan-out** after the header exists: subagent A `functions` + `classes` + `messages`
parsing and tests; subagent B `statics` + `slot_defaults` + `exports/free` + the
manifest-driven suite. Disjoint functions in `src/module.c`, coordinate via one shared
`module_load_section_*` naming scheme fixed by the Opus session.

### M5 — CLI and documentation
**Scope**
- `src/wyrm/main.c`: real argument parsing (`wyrm [--sections] [--disasm] file.wyc`);
  loads via `wy_module_load_bytes`; `--sections` prints the summary shown in the exit
  criterion; `--disasm` prints one line per instruction using `wy_opcode_names` and the
  operand decode macros from `opcode.h` (format like pypoc's `.wy_a` code lines so they can
  be diffed: `word  op mnemonic operands`); no execution yet: print `not run: interpreter
  lands in epic 2` and exit 0. Remove the `w_do_a_mul` demo. File reading through the
  context allocator, not raw `malloc`.
- `scripts/check_disasm.sh`: for each manifest row, diff `wyrm --disasm` against the
  `SECTION code` lines of the `.wy_a` (comments stripped) — a cheap check that decoding
  agrees with the compiler. Run it once; add as a meson test if it is stable.
- `doc/vm_impl.md` rewritten as the agent map: file → responsibility table, the
  `wy_module` table layout, how to regenerate the corpus, how to run one fixture.
  `doc/EXPLAINER.md` "Bytecode / VM state" section updated to reflect the loader.

**Files** changed: `src/wyrm/main.c`, `doc/vm_impl.md`, `doc/EXPLAINER.md`; new:
`scripts/check_disasm.sh`.

**Acceptance** the exit-criterion commands; `scripts/check_disasm.sh` reports 0
mismatches across the corpus.
**Model** Sonnet.
**Fan-out** none.

## Out of scope / deferred

- Executing any instruction (epic 2). `main.c` must not attempt to run init.
- Builtin fill of `free` slots (epic 2), message binding (epic 4), imports (epic 5).
- Replacing the scaffold symtab (epic 2); this epic only routes interning through one
  function.
- `wy_bytes` object (epic 2); binary statics are held in a placeholder.
- Big-endian hosts: reject with `WY_ERR_IMAGE` for now and note in `doc/vm_impl.md`.

## Risks

- **Corpus churn**: every compiler fix in pypoc (e.g. epic 4's message promotion)
  regenerates fixtures and shows as large diffs. Mitigation: `.wy_a` is committed next to
  `.wyc` so diffs are readable; `build_corpus.py` is deterministic; commit corpus
  regenerations as their own commits.
- **Header drift**: pypoc's `opcodes.py` may change under this repo. Mitigation: the sync
  test fails loudly; the sync script is the only way headers change.
- **Fragment samples**: some `samples/*.wy` need harness names and cannot run
  standalone. Mitigation: `fragment` status in the manifest; never treated as failures.
- **Old-format assumptions elsewhere**: `wy_exec_fn_b_code_pack` limits (4096 modules, 1M
  words) and `WY_MAX_ARRAY_LEN` may bite large corpora later. Mitigation: assert against
  them at load and note in the report.

## Report

Beyond the README template, `epic_1_report.md` must record: the corpus counts per
manifest status; the exact list of `pypoc/test/test_vm_load.py` rejection cases ported and
any not ported (with reason); the representation chosen for binary statics and doubles
pending epic 2; the interning entry point name the loader uses; whether `check_disasm.sh`
found any decode disagreement; the test count before/after; and proposed edits to
`epic_2.md` (in particular: does `wy_module` need fields the design did not foresee, and
is `wy_slot_dict` adequate for exports/free or does epic 2 need a general symbol→value
dict).
