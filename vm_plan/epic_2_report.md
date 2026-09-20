# Epic 2 report — Foundations and core interpreter

Session dates: 2026-09-15 · Models used: Sonnet 5 (coordinator) + Sonnet 5 subagents (5,
one per fan-out slice: M1 symtab/strings, M1 heap object kinds, M5 builtins, M5
link/run_init/CLI) · Commits: `4b6ddfc`..`db4670f`

## Landed

- **M1** — Type tag enum reordered to match design §4 (adds `PTYPE`; `TUPLE`, `LIST`,
  `BYTES`, `FUNCTION`, `NATIVE`, `INSTANCE`, `MESSAGE`, `BOUND_MSG`, `COROUTINE`, `ITER`
  appended). Fixes `wy_value_uword` mistagging `WORD`. Real symtab
  (`include/wyrm/symtab.h`/`src/symtab.c`) replaces the 8KB scaffold: FNV-1a over the
  wyc-format.md §8.4 31-codepoint significant prefix, open addressing, doubling growth, no
  length cap. String hashing switched to FNV-1a. Six new GC-tracked heap kinds:
  `wy_tuple`/`wy_list`/`wy_bytes`/`wy_error_obj`/`wy_function`/`wy_native`.
  `test_cwyrm --test-suite="symtab,heap_objects"` green.
- **M2** — `wy_gc_object_visit` rewritten as an iterative gray-worklist mark (grown via the
  arena allocator; abandons the *sweep*, not just the current visit, if the worklist can't
  grow, so a partial mark never frees a live object). `wy_context` gains
  `gc_pressure`/`gc_threshold` + `wy_context_gc_safepoint`, a `roots[64]` stack +
  push_f/pop_f, and a `builtins` GC root. `test_cwyrm --test-suite="wgc"` green including
  a 10k-pair-chain mark and a `gc_threshold = 0` stress case.
- **M3** — `wy_fiber_frame` replaced by the tagged `wy_frame` (`include/wyrm/frame.h`);
  native-frame push/pop/tail-call rewritten with no behavioral change (`test_fiber.cpp`
  passes unmodified). `WY_EXEC_SWITCH`/`WY_EXEC_FAULT` added; `wy_fiber_exec_f` surfaces a
  fault as `WY_ERR_FAULT` (`fiber->fault` readable); `wy_context_exec` loops across
  switches. Native bridge (`src/vm_call.c`): `wy_vm_call_leaf_f`,
  `wy_vm_call_exec_push_f`, `wy_vm_native_await_complete_f`, unit-tested standalone before
  the loop existed. `test_cwyrm --test-suite="vm_call"` green (6 cases).
- **M4** — `wy_vm_run` (`src/vm.c`): the dispatch loop, no C recursion across
  bytecode-to-bytecode calls/returns. Opcodes: `noop`, `trap`, `return` (WINDOW backfill),
  `lnil`/`lbool`/`lunset`, `i8`/`i32` (wide), `move`, `gget`/`gset` (fault on Unset),
  `lsym`, `lconst`, `neg`/`inv`/`not`, `jf`/`jt`/`jerr`/`jnerr`/`jmp` (compact+wide), `f32`,
  `add`..`bxor`/`eq`..`ge`/`cmp3`/`is` (`src/vm_ops.c`), `call` (FUNCTION + leaf NATIVE),
  `closure` (0 captures). `wy_vm_call_sync` implemented here (see Deviations).
  `test_cwyrm --test-suite="wvm"` (the added "vm dispatch loop" suite) green, 16 cases
  covering both encodings where paired, backfill truncation/nil-fill both directions, a
  nested bytecode call through a closure, wrong-arity rejection, `trap` fault.
- **M5** — `src/builtin/builtins.c` (`wy_builtins_new`): synthetic builtins module,
  `println`/`print` leaf natives + `nil`. `src/link.c` (`wy_link_fill_from_builtins`):
  layer 3 of the three-layer fill. `wy_module_run_init` (`src/module.c`). Real CLI
  (`src/wyrm/main.c`): creates a fiber, installs a stdout `io.write`, links, runs init.
  Golden harness (`src/test/test_bytecode_golden.cpp`, meson suites
  `golden`/`golden-gcstress`). **Exit criterion passes exactly as specified**:
  ```
  ./buildDir/src/wyrm/wyrm test/bytecode/hello.wyc          # prints: Hello World
  meson test -C buildDir --suite golden                      # 2/2 OK
  ```
  All seven target fixtures (hello, hello_1, hello_2, hello_3, arith, control_flow,
  multiret) match their `.out` byte-for-byte, including under `gc_threshold = 0` stress.

## Deviations from the epic file

- **`wy_vm_call_sync` implemented in M4, not M5.** design_c_vm.md §9a's own testing
  strategy needs it to drive hand-packed fixtures via `wy_vm_call_sync` before
  `wy_module_run_init` exists; M4's file list didn't call it out but M4's tests couldn't
  work without it. `wy_vm_call_continue` is still just a declaration (`WY_ERR_NOSUPPORT`) —
  genuinely epic 3+ (natives calling back into the VM).
- **No `packed_ops.h` / `pypoc/tools/generate_c_fixtures.py`.** Time-boxing this session
  to the six-fixture target rather than also standing up the pypoc-side generator script.
  `src/test/test_wvm.cpp` hand-packs instruction words directly instead (helper functions
  `enc1`/`enc2a`/`enc2b`, a synthetic-module builder `run_synthetic`/`make_synthetic_module`
  mirroring the design's own `wy_module_new_synthetic` idea). Covers the same ground for
  this epic's opcode set but doesn't scale cleanly — **recommend building the real
  generator before epic 3** adds many more opcodes (tuples, dicts, iteration, unpack) to
  hand-pack by hand.
- **`wy_value_is_error` bug found and fixed mid-epic.** My own first implementation
  (M1) excluded Unset (`{ERROR, NULL}`) from "is an error", contradicting both my own doc
  comment and `pypoc/wypoc/wyrm_builtins.py`'s `is_error` (which is true for Unset too).
  This silently broke `jerr`/`jnerr` and therefore `?=` (control_flow.wy's `defaulted`
  fixture) until caught by the golden harness — fixed by dropping the non-NULL check
  entirely (`value.type == WY_TYPE_TAG_ERROR` is now sufficient). **If epic 3 adds
  INSTANCE-class-based errors, extend this function, don't narrow it back.**
- **`wy_bson_get_str` bug found and fixed mid-epic (not epic 2's own code, but blocking
  it).** It returned a BSON string's on-disk length *including* the trailing NUL
  (wyc-format.md §4.2), not the content length every other accessor in `bson.h` returns.
  Existing epic-1 tests never caught it (they compared against NUL-terminated C string
  literals, so the extra byte was invisible); `wy_string_concat` (new this epic) exposed it
  immediately as an embedded `\0` in every decoded string static. One-line fix
  (`include/wyrm/bson.h`), covered indirectly by the golden harness now that `hello.wyc`'s
  `"Hello " + name` actually runs.
- **`gget` faults on *any* Unset global**, not specifically a free-name slot (design's own
  text says "fault on Unset *free* slot"; the format spec's plain-language rule is looser
  and simpler — "gget... faults the way reading any declared-but-unassigned variable does"
  — and that's what's implemented). No ambiguity-marker handling (nothing produces one
  yet; epic 2/M6's wildcard imports are what would).
- **Argument binding is fast-path only**: `argc == nparams` exactly, or the call faults
  with `WY_ERR_ARITY` translated to a fault message. No defaults, no `*args`/`**kwargs`, as
  the epic file's own M4 scope specified.
- **`wy_string_concat`/`wy_string_cmp_f` added to `string.h`/`.c` in M4**, not built by the
  M1 symtab/strings subagent (design listed them under M1's scope but the subagent's brief
  didn't explicitly call them out and it reasonably didn't add unrequested API surface).
  Needed for `add` on STR and comparisons; added when M4 needed them.

## Tests

- Before epic 2 (epic 1 baseline): 99 cases / 650 assertions.
- After M1: 114 cases / 14829 assertions (the 2000-symbol growth-stress case and several
  GC-pass cases dominate the assertion count).
- After M2: same case count, +3 wgc cases (10k-pair-chain, gc_threshold=0, root push/pop).
- After M3: 120 cases / 14857 assertions (+6 vm_call).
- After M4: 136 cases / 14923 assertions (+16 dispatch-loop).
- After M5 (through the `wy_value_is_error` fix and golden harness): **161 cases / 15132
  assertions**, all green, including the two new meson suites (`golden`, `golden-gcstress`)
  wired as real `test()` entries so `meson test -C buildDir --suite golden` works exactly
  as the exit criterion specifies.
- Corpus: unchanged from epic 1 (30 matches / 10 REFUSED / 1 DIVERGES at the loader level);
  epic 2 doesn't regenerate the corpus, it runs seven of the "matches" fixtures for real.

## Type tags, integer/float/division rules (for the report per epic_2.md's own ask)

Final `wy_type_tag` order (`include/wyrm/primitive.h`): `NIL, BOOL, WORD, UWORD, FLOAT,
SYMBOL, PTYPE, GC_PATH_START, ERROR, PAIR, BOX, OBJECT, STR, FIBER, DTYPE, CLASS, MODULE,
TABLE, TUPLE, LIST, BYTES, FUNCTION, NATIVE, INSTANCE, MESSAGE, BOUND_MSG, COROUTINE, ITER`
— matches design_c_vm.md §4 exactly.

Pinned against `pypoc/wypoc/wyrm_eval_parse_tree.py`'s `BINOPS` table
(`src/vm_ops.c`, tested in `test_wvm.cpp`'s arithmetic/division-by-zero cases and
confirmed against `multiret.out`'s literal `3.4`):
- `/` is **always true division**: even `WORD / WORD` produces a `FLOAT` (Python 3 `/`
  semantics, not C integer division). Confirmed this is correct, not a bug, by working
  `17 / 5 = 3.4` back through `multiret.wy`'s `divmod` fixture to `multiret.out`'s first
  line.
- `%` keeps Python's sign convention (result takes the sign of the divisor) for both int
  and float paths.
- Division/modulo by zero and a negative shift count produce an **error value** (a plain
  `wy_error_obj`, not yet a typed error class — that's epic 3/4), not a VM fault. Confirmed
  against `pypoc`'s `_safe_div`/`_safe_mod`/`_safe_shift` wrappers, which catch the Python
  exception and return a `wyrm_builtins.error(...)` value instead of raising.
- `pow` on two ints uses integer exponentiation (repeated squaring); either operand a float
  uses `pow()`. Negative int exponents are not specially handled (would currently produce
  1 via the `exp <= 0` early return in `int_pow_`) — untested by the six target fixtures,
  flagged for epic 3 if `**` with a negative int exponent ever needs to match Python's
  float-promoting behavior.

No epic-3 opcode was pulled forward into epic 2's scope. `iter`/`itnext` appear in
`control_flow.wy`'s compiled output (`first_big`'s `for`/`else` loop) but that function is
never called by the fixture's top-level code, so the six/seven target fixtures never
actually execute those opcodes — confirmed by successfully running `control_flow.wyc`
without implementing them; `default:` in `wy_vm_run`'s switch would fault on them if
reached.

## Builtin list and formatting rules

`println`, `print` (leaf natives), `nil` (constant) — the only three the six target
fixtures reference (confirmed by the epic's own scan grep of each fixture's `free`
section). Both take 0–255 arguments. `println` space-joins arguments and appends `\n`;
`print` space-joins arguments with **no** trailing newline (this "print also space-joins"
detail corrects an assumption written into the M5 builtins subagent's own brief — verified
byte-exact against `hello_3.out`'s `"Magic Number:  3 \n"`, which has two spaces before `3`
precisely because `print`'s join adds one on top of the literal's own trailing space).
Formatting: WORD/UWORD decimal; FLOAT via true shortest-round-trip search (`%.*g`
precision 1–17, first one that reads back bit-identical via `strtod` wins, `.0` appended if
the result has no `.`/`e`); BOOL `true`/`false`; NIL `nil`; STR raw content, no quotes;
Unset `Unset`; anything else best-effort (not exercised by the target fixtures).

## Timing (informal, wall-clock, process-startup dominated)

1000 sequential `wyrm test/bytecode/hello.wyc` invocations: ~1.77ms/run. Same for
`arith.wyc`: ~1.40ms/run. This is **not** a meaningful interpreter-throughput number — each
invocation pays full process/machine/context/fiber/builtins-module construction, which
almost certainly dominates the few dozen instructions either fixture actually executes.
Treat this only as "nothing is pathologically slow," not as a real baseline; epic 6
("hardening, performance") is where a real benchmark harness belongs.

## design_c_vm.md sections amended

- **§1.3** (native bridge): the exec-native-from-bytecode path is built (`vm_call.c`) and
  unit-tested standalone, but `wy_vm_run`'s `call` opcode does not yet wire it in — calling
  an exec NATIVE from bytecode faults "not supported until epic 3+" rather than actually
  bridging. The reservation-vs-window addressing mismatch the epic's own risk section
  flagged is real (a bytecode frame's `L`/`P` are raw pointers with no relationship to the
  fiber's `value_stack.base`-relative reservation indexing that `wy_fiber_result_n` uses),
  and resolving it properly is left as explicit epic-3+ work rather than papered over.
- **§2** (fault path): `unwind_on_fault_f` does not drain defers (correctly, per M4's own
  "no defers yet" scope) — it just walks `current_frame` back to the nearest native frame
  and resets `value_stack.top` to the shallowest discarded bytecode frame's `p`, reclaiming
  the whole discarded region in one step. Epic 3 needs to interleave defer-closure pushes
  into this unwind rather than a plain pop loop.
- No other sections needed correction; the frame model, GC design, and symtab design all
  matched implementation with no surprises worth recording here beyond the Deviations
  section above.

## Open questions and known gaps

- `is` only resolves the primitive-type-name-string operand (`value is "int"`); class and
  tuple operands fault. Sufficient for `arith.wy`'s `typed` fixture; epic 3/4 territory
  otherwise.
- No line-number-in-fault-message support (the epic file itself calls this "nice to have,
  skip unless trivial" — skipped; `main.c`'s fault path prints the error object's `what`
  string only, no word offset or source line).
- Big-endian hosts still rejected outright (unchanged from epic 1, not touched this epic).
- `wy_dict` is unchanged from whatever epic 0/2's starting state was — epic 3's own
  assumptions list already flags this for its own scan, nothing new to add.

## Proposed edits to epic_3.md

Answering epic_3.md's own state-scan checklist from what this session already knows, so
epic 3's scan session can skip re-deriving it:

1. `wy_vm_run`'s structure matches design §2's push/reload shape, `fr->ip` saved only on
   frame exit — **confirmed true**, no re-verification needed.
2. `wy_frame` **does** have `defers` (`wy_pair*`) and `aux` fields already (design §1.1's
   full struct landed in M3, not just the native-path subset) — they're simply unused
   until epic 3 wires up defer draining and `WY_RET_CONSTRUCT`/etc. **Confirmed true,
   nothing to build here, just start using the fields.**
3. Type tags landed in full this epic (item 3 of epic_3.md's checklist): `TUPLE`, `LIST`,
   `BYTES`, `FUNCTION`, `NATIVE`, `ERROR` (as `wy_error_obj*`) all have real heap-object
   shapes (M1), not stubs. `INSTANCE`, `MESSAGE`, `BOUND_MSG`, `COROUTINE`, `ITER` are tag
   values only — no backing struct yet (epic 4/5 build those).
4. `wy_dict` — **not touched this epic**, still whatever epic 0's original shape was; epic
   3's own assumption that it "needs a real hash table before `collections.wyc` is fast
   enough for the GC-stress run" is unverified by this session, re-confirm in epic 3's scan.
5. Test baseline for epic 3's "before" line: **161 cases / 15132 assertions**.
6. `test/bytecode/{closures,collections,errors}.wyc`/`.out` — not checked this session
   (out of the six/seven-fixture target), confirm in epic 3's own scan.
7. `wy_builtins_new` (`src/builtin/builtins.c`) exists; leaf natives registered via
   `wy_native_leaf_new` + a hand-built `exports` slot dict on a synthetic `wy_module`
   (`state = WY_MODULE_BUILTIN`). Only `println`/`print`/`nil` are registered — epic 3's
   M4 (builtin registrations per its own context-loading list) will need to add more
   (`str`/`int`/`float`/`bool`/`len`/pair `cons`/`car`/`cdr`/etc.) following the same
   pattern.
8. `getidx`/`setidx`/`iter`/`itnext`/`unpack`/`in`/`is`/`cmp3`/`plist`/`dict` opcode names
   — **confirmed present** in `include/wyrm/opcode.h` (verbatim-synced since epic 1). `is`
   and `cmp3` are already implemented (this epic, `vm_ops.c`) for the primitive-type-name
   and WORD/FLOAT/STR/BOOL cases respectively; epic 3 extends `is` to class/tuple operands
   and doesn't need to touch `cmp3` beyond adding INSTANCE dispatch.
9. `pypoc/.venv/bin/wyrm` — present (epic 1's toolchain step), unchanged.
10. GC stress mode (`gc_threshold = 0`) — **confirmed built** this epic (M2), and
    exercised end-to-end by the `golden-gcstress` meson suite, not just synthetic GC
    tests. Not scope creep to redo in epic 3; just reuse `context->gc_threshold = 0` and
    add fixtures to `test_bytecode_golden.cpp`'s pattern (or a new suite) as
    `closures.wyc`/`collections.wyc` land.

Additionally: **`wy_value_is_error` now returns true for Unset** (see Deviations above) —
if epic 3/4 adds INSTANCE-class-based errors, extend the function's check rather than
narrowing the ERROR-tag branch back to non-NULL-only, or `jerr`/`jnerr`/`?=` will silently
break again the way they did mid-this-epic.

## Orientation for the next session

- Start with `doc/vm_impl.md`'s epic-2 agent map (added this session, above the epic-1
  one) — it points at every file this epic touched.
- `src/vm.c`'s `wy_vm_run` is the dispatch loop; `default:` in its opcode `switch` faults
  cleanly on anything unimplemented, so adding an opcode is additive, not a rewrite.
- `src/test/test_wvm.cpp`'s `run_synthetic`/`make_synthetic_module`/`enc1`/`enc2a`/`enc2b`
  helpers are the fastest way to hand-pack a new opcode test without a real `.wyc` — but
  seriously consider building `pypoc/tools/generate_c_fixtures.py` first if epic 3 needs
  more than a handful of new hand-packed cases (tuples/lists/dicts/iteration have a lot of
  operand shapes to get right by hand).
- `src/test/test_bytecode_golden.cpp` is the strongest regression check available — it
  runs real `.wyc` fixtures through the whole stack (load → link → init → dispatch loop →
  output capture) and diffs against committed `.out`. Adding a fixture to its `TEST_CASE`
  list (both `golden` and `golden-gcstress` suites) is the highest-value single addition
  epic 3 can make as each new fixture (`closures`, `collections`, `errors`) starts passing.
- `wy_context_root_push_f`/`pop_f` exist (M2) but nothing calls them yet outside their own
  test — epic 3's binding/defer work is the first real candidate (holding a freshly
  allocated closure/error object across further allocations before it's reachable from any
  frame).
- The M1/M5 fan-out (four Sonnet subagents this session, each given a precise struct/
  function-signature spec and an explicit "files you must not touch" list) worked cleanly
  with zero merge conflicts — same pattern is worth reusing for epic 3's own independently
  fan-out-able pieces (e.g. one agent per new opcode family, one for the builtin
  registrations, following the epic file's own model-staging table).
