# Epic 1 report — Toolchain, conformance corpus, image loader

Session dates: 2026-09-15 · Models used: Sonnet 5 · Commits: `69c7478`..`25b7da0`
(after the pre-existing `e8b3304` housekeeping commit)

## Landed

- **M1** — `scripts/setup_pypoc.sh` and `scripts/build_corpus.py` generate the
  committed corpus. `scripts/setup_pypoc.sh && pypoc/.venv/bin/python
  scripts/build_corpus.py` now passes; `git status --short test/bytecode |
  wc -l` → 97; `grep -c matches test/bytecode/manifest.txt` → 30.
- **M2** — `opcode.h`/`image.h` adopted verbatim via
  `scripts/sync_pypoc_headers.py`; `opcode_names.h` generated.
  `scripts/sync_pypoc_headers.py && git diff --stat include/wyrm` is clean
  after the first run; `meson test -C buildDir` green.
- **M3** — `src/image.c`'s `wy_image_from_bytes` and `src/bson.c`'s
  strictness pass. `./buildDir/src/test/test_cwyrm --test-suite="image,bson"`
  green, 16 rejection cases.
- **M4** — `wy_module` rewritten to design_c_vm.md §5; `wy_module_load_image`/
  `wy_module_load_bytes` load every section. `meson test -C buildDir --suite
  loader` green; `./buildDir/src/test/test_cwyrm --test-suite="module"` green.
- **M5** — real `wyrm` CLI (`--sections`/`--disasm`), `scripts/check_disasm.sh`,
  docs. Exit criterion commands all pass exactly as specified:
  ```
  meson test -C buildDir --suite loader   # OK
  ./buildDir/src/wyrm/wyrm test/bytecode/hello.wyc --sections
  # header n=hello v=1 g=2 l=3 | statics 2 | functions 1 | code 15 words | exports 1 | free 1
  ```
  `scripts/check_disasm.sh` → "checked 31 fixtures, 0 mismatches".

## Deviations from the epic file

- **`include/wyrm/image_loader.h` (new, not in the epic's M3 file list)**
  holds `wy_image_from_bytes`'s declaration. It can't live in `image.h`
  itself: that header must stay byte-identical to pypoc's copy
  (`test_headers_sync.cpp` diffs it verbatim), so a VM-only function
  declaration would be silently deleted on the next `sync_pypoc_headers.py`
  run.
- **`WY_TYPE_TAG_BOOL` added alongside the planned `WY_TYPE_TAG_FLOAT`.**
  The epic only called out float needing a new tag; bool has no existing
  representation in `wy_type_tag` either (nothing conflates it with WORD),
  so `statics`/`slot_defaults` bool values got the same treatment.
- **A pre-existing bug in `src/machine.c`'s scaffold symtab, fixed.**
  `wy_machine_find_symbol`/`wy_machine_insert_symbol` returned a pointer to
  an entry's length-prefix byte instead of the text after it. Harmless as
  long as nothing compared symbol *content* — which nothing did before M4's
  tests read function/class names back and got garbage first bytes. One-line
  fix in both functions (return `&data[offset + 1]`), covered by M4's tests
  reading real names (e.g. `hello.wyc`'s function name `"greet"`) rather than
  a dedicated regression test for the symtab itself.
- **`wy_module_load_bytes`/`wy_module_load_image` reject big-endian hosts**
  (`WY_ERR_IMAGE`) via a runtime check, per the epic's own deferred-items
  list. Untested (no big-endian CI host), documented in `doc/vm_impl.md`.
- **Two extra meson `test()` entries**, not literally called for by the
  epic text but needed to make its own acceptance commands work as written:
  `cwyrm-loader` (suite `loader`, so `meson test -C buildDir --suite loader`
  is a real filter) and `disasm-check` (suite `disasm`, wraps
  `check_disasm.sh`, skipped rather than failed if the script or corpus is
  absent).

## Tests

- before epic 1: 75 cases / 453 assertions
- after M2 (old opcode tests removed, headers-sync test added): 75 / 455
- after M3 (image + bson strictness): 94 / 506
- after M4 (module + loader suite): 99 / 650
- after M5: 99 / 650 (no new doctest cases; `disasm-check` is a separate
  meson test, not a doctest case)
- corpus: 30 `matches`, 10 `REFUSED`, 1 `DIVERGES` (41 manifest rows total;
  31 have a `.wyc` to load)

## `pypoc/test/test_vm_load.py` rejection cases: ported vs not

Ported, as byte-mutation tests against the committed `hello.wyc`
(`src/test/test_image.cpp`) or hand-built minimal documents
(`src/test/test_bson.cpp`):

- `test_rejects_a_short_file`
- `test_rejects_bad_magic`
- `test_rejects_a_future_container_version`
- `test_rejects_an_unknown_section_id`
- `test_rejects_an_unsorted_directory`
- `test_rejects_a_duplicate_section_id`
- `test_rejects_a_section_running_past_the_end`
- `test_rejects_a_section_overlapping_the_directory`

Plus BSON-level cases the pytest suite covers indirectly through image-level
tests, ported directly against the BSON reader: non-permitted tag, bad bool
byte, binary subtype ≠ 0, a field that overruns its document, and a `0x00`
byte that appears before the document's declared end.

**Not ported as dedicated tests**, though the rule they check is enforced by
general logic elsewhere:

- `test_rejects_a_missing_code_section` — `wy_image_from_bytes` requires
  section 8 present (checked, exercised implicitly by every fixture load
  succeeding and by `test_rejects_a_section_overlapping_the_directory`'s
  sibling checks), but no single-byte-mutation test targets exactly this.
- `test_rejects_a_header_that_is_not_bson` — the general BSON strictness
  tests cover malformed BSON; no test specifically corrupts the `header`
  section's document length byte the way the pytest case does.
- `test_rejects_an_out_of_range_table_index` (superclass) and
  `test_rejects_an_export_pointing_at_no_slot` — both are M4-level
  (table-content) rules, not M3-level (container/BSON) rules the epic's M3
  scope names; `module.c`'s bounds-checks enforce both (every `super_slot`,
  `getter_fn`, dispatch slot, export/free index, etc. is checked against its
  table at load), but no dedicated single-mutation regression test exists
  for either yet.

Reason for the gap in all four cases: time-boxing this milestone to the
epic's explicit "at least 10 rejection cases" bar (16 landed) rather than
porting the full pytest suite line-for-line. Worth doing before epic 2 if
someone has spare time — these are cheap, mechanical ports.

## Representation choices pending epic 2

- **Doubles**: `WY_TYPE_TAG_FLOAT`, `data.fp` (`wy_float`, which is `double`
  on this 64-bit-cell build, `float` on a 32-bit one — a real precision gap
  on 32-bit, noted in `doc/vm_impl.md`, unexercised since nothing here builds
  32-bit today).
- **Bools**: `WY_TYPE_TAG_BOOL`, `data.flag` (not in the original design
  sketch; added because nothing else could represent one without lying).
- **Binary statics**: a `wy_string` with `WY_GC_FLAG_BINARY` set on its
  object header (new flag, `include/wyrm/gc_flags.h`). Placeholder until
  the `bytes` type lands (epic 7); nothing currently reads the flag.

## Interning entry point

`wy_context_intern(wy_context* context, const char* text, wy_uword len,
wy_symbol* out)` (`include/wyrm/context.h` / `src/context.c`). Every loader
function that needs a `wy_symbol` — module name, function/param/slot/class
names, export/free key names — goes through this one function, which wraps
`wy_machine_insert_symbol` (`src/machine.c`'s scaffold symtab). Swapping the
symtab in epic 2 means changing this function's body only.

**Known limitation carried into epic 2**: the scaffold symtab caps a symbol
at 127 bytes and compares full byte content rather than wyc-format.md §8.4's
31-codepoint significant prefix. Every symbol in the current corpus fits
comfortably under 127 bytes, so this hasn't bitten anything yet, but epic 2's
real symtab must implement the significant-prefix rule for correctness, not
just capacity.

## `check_disasm.sh` result

0 mismatches across 31 fixtures (every manifest row with a `.wyc`). Wired as
the meson test `disasm-check` (suite `disasm`), which skips rather than
fails if the script or the corpus is missing.

## Open questions and known gaps

- Big-endian hosts are rejected outright at load rather than handled; no
  test exercises this path (would need a big-endian CI target or a manual
  byte-swap harness).
- `wy_module`'s `wildcards` field is declared but always empty; nothing
  populates or frees it yet (finalize already handles the NULL case).
- The four un-ported rejection tests listed above.

## Proposed edits to epic_2.md

- **`wy_module` did not need fields the design didn't foresee.** The struct
  in design_c_vm.md §5 mapped onto the section schemas cleanly; the only
  additions were the two new `wy_value` tags (FLOAT, BOOL) and the
  `WY_GC_FLAG_BINARY` marker, neither of which touches `wy_module`'s own
  shape.
- **`wy_slot_dict` is adequate for `exports`/`free_names`** as built (a
  symbol → `wy_uword` slot index map) — nothing in M4 needed more than that.
  epic 2's three-layer fill (§7.2) does need something `wy_slot_dict` does
  not provide: **ambiguity tracking** for layer-2 fills (two wildcard
  imports supplying different values for one name). That's what
  `fill_layer`/`fill_source` (already declared on `wy_module`, currently
  unused) are for — epic 2 should treat those two parallel arrays, not a
  richer slot dict, as the mechanism, matching design_c_vm.md §5's sketch of
  `wy_link_fill`.
- Epic 2's scan should re-verify `wy_module_load_image`'s exact bounds-check
  coverage before assuming it's complete: every index epic 1 knew about
  (functions' `code_offset`/dispatch slots/param defaults, classes'
  slots/messages/statics/superclass/init, messages' symbol paths,
  exports/free indices) is checked, but epic 2 introduces new
  cross-references (e.g. resolving a message path across module boundaries)
  that epic 1 never had to validate.
- Consider spending a small amount of epic 2 time porting the four
  not-yet-ported rejection tests listed above before building on top of the
  bounds-check logic they'd exercise.

## Orientation for the next session

- Start with `doc/vm_impl.md`'s agent map and `wy_module` table — it's
  written to make epic 2's scan cheap.
- `src/vm.c`'s `wy_vm_exec_bytecode` is the interpreter loop's landing spot;
  it's currently a one-line stub.
- `wy_context_intern` is the loader's only interning path; epic 2 replaces
  its backing scaffold symtab (`src/machine.c`) without touching call sites.
- `test/bytecode/manifest.txt` + the `loader` test suite are the fastest way
  to check "does every image still load" after a structural change.
- `scripts/sync_pypoc_headers.py` and `scripts/check_disasm.sh` are safe to
  re-run any time; both are idempotent / read-only respectively.
- The four not-yet-ported `test_vm_load.py` rejection cases (see above) are
  low-hanging fruit if epic 2 has spare cycles early on.
