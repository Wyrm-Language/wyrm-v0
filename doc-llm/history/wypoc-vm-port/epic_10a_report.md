# Epic 10a report — Eval context and macro expansion (decorators)

Status: **M0–M5 landed** (M5's blocker, the pypoc-shaped DSL, was resolved by making ast.wy own
the tree shape; see "M5" below).
Nothing committed by this session (M0's commit `5065501` is the last); the working
tree holds M1–M5.
Session dates: 2026-09-19 · M0: GLM (opencode) · M1–M4: Claude (Sonnet 5).

## M1–M4 landed (uncommitted)

Design outcome versus the plan (the plan text in `epic_10a.md` is updated to match):

- **No exec native anywhere.** M1's `send` exec native became a *leaf* `bind_message(scope,
  recv, name)`: it looks `name` up in the scope module's `message_table`
  (`wy_module_message_lookup_f`), resolves the overload for `recv` and answers a
  `wy_bound_msg` exactly like `getmsg`; the wyrm caller then calls it normally. (`send` was
  also taken: it is the coroutine builtin.) Also new leaf builtins `tree_box(x)` and
  `sexpr(x)`; `TreeBase` now has one `__tree` slot (`context->tree_base_class`).
  `src/builtin/builtins.c`.
- **`std::expand::expand(tree, scope_image: bytes, entry: str) -> tree | error`** (M2), a
  hosted module (`src/platform/hosted/expand_native.c`, `wy_expand_module_install`, called
  from `src/wyrm/main.c` after `std::io`). It named `expand`, not `expand_decorators`:
  bare `expand` is the list builtin, so the primitive lives in its own module. It builds a
  fresh machine+context (`wy_cmachine_new`), copies the tree in (iterative, no recursion;
  nil/bool/numbers/str/symbol/pair/list/tuple only), loads the scope image, initialises it,
  `wy_link_import`s the entry module and calls `expand_tree(tree, scope)` with
  `wy_vm_call_sync` on the CHILD context, copies the answer out and destroys the child
  (`wy_cmachine_destroy_residual`; `wy_expand_leaked_bytes()` is asserted 0 by the tests).
  - **The child reuses the parent's `import_hook`/`import_ud`** (host code that reads the
    `.wyc` images); it never touches a file itself. So the plan's "parent supplies the import
    closure" collapsed into "the parent's own -I path resolves lazily". Consequence: the
    expander (`wyrm::compiler::expand`) and every decorator module must be on the compile's
    `-I` path as `.wyc` images.
  - D10 is enforced by construction: the child has builtins (`println` is a no-op, no io
    hook) and `std::expand` (which answers "expansion is not re-entrant" inside the child)
    and nothing else. An imported module needing `std::io` fails to load with a message
    naming the module ("expansion VMs have no host modules").
  - A decorator that faults names itself: the expander keeps `_STATE[0]` = the decorator
    name and the host reads it (`@name: ...`).
  - **GC trap found and fixed:** the child's module inits run GC safepoints; the copied-in
    tree was not rooted and got freed (a 5000-element list came back length 1). Everything
    C holds across VM runs is pushed with `wy_context_root_push_f` right after context
    creation.
- **`wy/wyrm/compiler/expand.wy`** (M3): the expander, pure wyrm, runs in the child. Outside-in,
  source order; applies the outermost decorator to the RAW rest, coerces the answer to the
  node's statement/expression position, re-walks the answer. Handles pair-list/native-list/
  tuple containers (the engine-duality trap above). Arguments per D9: pure literals as values,
  else `tree_box` of the tree. `template` falls back to `$['template, operand]` when the scope
  has no such message.
- **`compile_module`** (M4, `module.wy`) takes an optional `expander` callback; after the
  predefined-decorator pre-pass, if the tree still holds a `'decorated` node it calls
  `expander(body, module_name)` (error if none was supplied). The real expander is
  `wy/wyrm/compiler/expansion.wy`'s `expand_decorators` (passed by `compiler_main.wy`): it
  compiles a "scope" module of the module's own top-level imports and calls
  `std::expand::expand`. Modules without decorators pay nothing. The split keeps module.wy
  importable by pypoc's interpreter (image.wy's package-relative `import bjson::*` only
  resolves on the C VM; function-level imports are not supported by the bytecode compiler);
  `wy/std/expand.wy` is a stub so pypoc can import `std::expand` where the amalgam keeps it.
- **VM fix: `f(*xs)` (call_va/msg_va) accepts a plain list**, not only a tuple: both
  compilers pass a list for a bare `*xs`; the C VM demanded a tuple ("call_va: expected a
  positional tuple"). Also call_va now calls `bound_msg` values. `src/vm.c`.
- **Compiler builtin-name list** (`compiler/context.wy` BUILTIN_NAMES) gained `bind_message`,
  `error_message`, `tree_box` (the port refused any module using them: undefined name).
- **`_dsl.wy` imported `replace, chain` but used `cadr`** — worked in the amalgam, unbound in
  a real module tree. Now imports it. (Amalgam-hides-bugs again.)
- **`parser.wy` no longer imports `std::io`**: its `__main` demo moved to
  `wy/wyrm/tools/parse_dump.wy`. A scope module holds ALL of a module's imports, and
  `std::io` does not exist in the expansion VM.
- **Scripts:** `run_corpus_sweep.py` builds the expander (`build_expander`: the gen0 driver
  compiles `expand.wy`, pypoc cannot: unknown builtins), puts `out_dir` on `-I` (a decorated
  fixture's decorator module is compiled earlier in the same run), and compiles two
  port-only fixtures. `run_selfcompile.py` adds `expand.wy` to `SELF_SOURCES`.

Tests: golden fixtures `test/bytecode/expand/` (README there): `treemain` (tree_box/sexpr/
bind_message), `expandmain` (std::expand: round-trip of deep/wide/mixed trees, non-tree answer,
re-entrancy, missing entry, faulting decorator named, disallowed import, missing module),
both with gcstress variants; `wydecorated`/`wydeclib` (port-shaped twin of
`decorators/decorated`, compiled by the port in the sweep, output == pypoc's `decorated.out`).
Corpus sweep: 23 fixtures, 22 matches, 0 unexpected (`decorators/decorated` is an
EXPECTED_DIVERGES: pypoc's `declib` builds pypoc's 8-field `'fn` node, the port's parser
emits `'fn_def`).

## M5 landed (uncommitted)

`python3 scripts/run_selfcompile.py`: all 26 self-sources compile strictly through the port,
**including `wyrm/parser.wy` (86 `@accept` sites, expanded in throwaway VMs)**; gen1 == gen2
**byte-for-byte**. The interim "pypoc builds parser.wyc" arrangement is gone; the only
pypoc-built code left is gen0 (the amalgam driver).

What it took (the plan had scoped it as glue; it was not):

- **`ast.wy` owns the tree shape** (the design intent): `WY_SHAPE` is detected from a probe
  template in ast.wy itself (`car(sexpr(_shape_probe::$ast)) == 'fn_def`: pypoc's compiler yields
  `'fn`, the port `'fn_def`, so the shape follows whichever compiler built the module).
  Everything shape-dependent that `_dsl.wy` used to hard-code goes through ast.wy helpers:
  `s_kind`/`s_get_name`/`s_call_args`/`s_is_drop`/`s_is_pairlist`/`s_pairlist_items`/`s_fn_body`
  (reads) and `mk_name`/`mk_call_expr`/`mk_do`/`mk_if`/`mk_is_type`/`mk_index`/`mk_this_send`
  (builds, pair lists in the port's shape, native lists in pypoc's), plus `NIL_EXPR`.
  pypoc-mode behaviour is unchanged (pypoc's parser.wyc is byte-identical to before).
- **Hole substitution needed structure awareness.** The port's identifiers are bare symbols, so
  `replace`'s "match a name node" would also rewrite `tok.kind`'s member name when a hole is called
  `kind`. `s_substitute` (WY_SHAPE) walks nodes and leaves `attr`/`message` member names, and
  `sym/str/int/float/char/type` literals, alone.
- **The port's `fn $name` = "TreeBase method" shorthand was wrong and is removed** (module.wy):
  pypoc has no such rule; parser.wy's `$_mk_*` are plain functions called by name. The special
  case left them declared-but-never-set, so `$_mk_import_item` was an unbound global in the
  port-built parser. Consequence: `_dsl.wy`'s twelve `$` templates now stub like the other two
  (`this` in a plain fn under `@template`): the expected TSTUB set is 14, not 2.
- **`std::pairs::list_tail` used `for i in range(0, n)`, which never iterates on the C VM**
  (epic 6 gap in the notes); now a `while` loop. (Root cause of the empty `mk_expand_n` body.)
- **The expander unwraps an expression statement** for a single decorator (`@accept X` is
  `(decorated d (expr_stmt X))` in the port's trees; pypoc hands decorators the bare `X`); the
  answer is re-wrapped by the position coercion.
- **CLI fiber enlarged** (`src/wyrm/main.c`: 4096 -> 65536 values, 256 -> 4096 frames): deeply nested
  expanded `@accept` sequences overflowed the compiler's recursion.
- **gen0 must rebuild the expansion support itself** (`run_corpus_sweep.build_expander`): pypoc
  builds `_dsl.wyc`/`ast.wyc` pypoc-shaped, so the gen0 driver compiles them (decorator-free) over
  pypoc's copies before it compiles `parser.wy`.
- **Verification beyond the fixed point:** the port-built `parser.wyc` (128040 bytes; pypoc's is
  132088, so NOT byte-identical, the epic's ledger item) yields **byte-identical parse trees to the
  pypoc-built parser on ~50 sources (wy/, test/wy/, fixtures; 1.08 MB of output)**.

Known gaps (unchanged): `macroexpand` not provided; `sexpr(this)` does not expand nested
decorators; a lone `@template` bypasses the scope (shadowing). Decorators written against pypoc's
shapes (`decorators/declib.wy`) do not work under the port (the shape-aware helpers exist to
write ones that do; `test/bytecode/expand/wydeclib.wy` is one).

## Landed

- **M0 — Templates (`@template`, `::$ast`, strict lowering)** — acceptance, all verified:
  - `meson test` 8/8 (incl. new golden `template` + `template gcstress` doctests);
    `run_wy_tests.py` 7/7 (incl. new `test/wy/test_compiler_templates.wy`, 12 checks).
  - `python3 scripts/run_corpus_sweep.py`: 21 fixtures, 20 matches, 0 unexpected
    (new `template` fixture: walker `.out` == pypoc-built `.wyc` on the C VM ==
    port-compiled `.wyc` on the C VM). `decorators/*` + `samples/decolib` still REFUSED
    pending M3, as recorded in `EXPECTED_DIVERGES`.
  - `python3 scripts/run_selfcompile.py`: all 22 self-sources compile **strictly**
    (`predefined.wy` joined `SELF_SOURCES`; 24 images/tree with parser.wyc + wyrm.wyc),
    **gen1 == gen2 byte-for-byte**, and the only stubs are the expected
    `TSTUB _dsl::_tmpl_bool` / `_tmpl_opt` (template-marked, `this` in a plain fn).

  How it works (where to look):
  - `wy/wyrm/compiler/predefined.wy` (new) — the pre-pass. Rewrites `@template X` to
    `$['template, X]` for the sole or innermost decorator; outer decorators keep their
    `'decorated` around the wrapper (M3's outside-in expansion sees the tree it would
    have seen). Pure rebuild, no mutation. Called first by `compile_module`
    (module.wy), before the declare pass.
  - `wy/wyrm/compiler/expressions.wy` — `name::$ast` resolves in `_scope` by leaf
    spelling (`AST_FIELD`), via `_astref` → `emit_value` (the `$[...]` tree-constant
    path). Errors worded per pypoc: unknown definition / non-name operand.
  - `wy/wyrm/compiler/functions.wy` — `FnContext.tolerant` threaded per definition
    (`compile_callable`/`compile_function`/`compile_closure_expr`/
    `compile_nested_fn`, and classes.wy's `compile_dispatched_fn_toplevel`);
    `_compile_or_stub` stubs when `frame.tolerant` even in strict mode. Unlowered
    report entries are now `[name, reason, is_template]`.
  - `wy/wyrm/compiler/module.wy` — `'template` toplevel handler (fn/co get
    `tolerant=true`; class and plain statements compile as written); `_collect_definitions`
    (pypoc's `_collect_definitions` over wyrm shapes, whole-tree walk, wrapper removed,
    first-spelling-wins) — the index `::$ast` reads; declare pass keeps template-wrapped
    definitions' bindings identical to bare ones (plain fn name declared, `$`-fn and
    dispatched fn not). `stub_unlowered` default false.
  - `wy/wyrm/compiler/statements.wy` — `'template` statement handler one frame down
    (nested `@template fn` via the hook's new 4th arg).
  - `wy/wyrm/_dsl.wy` — the 14 templates marked (`$token_kind`, `$_text_tmpl`,
    `$soft_keyword`, `$_expect_tmpl`, `$_many`, `$_manyflat`, `_tmpl_bool`,
    `$_tmpl_value`, `$_tmpl_push`, `$_tmpl_cons`, `_tmpl_opt`, `$_binop_or`,
    `$_accept_cache`, `$_expect`). pypoc still compiles this file unchanged
    (`@template` is a pass-through there).
  - `wy/wyrm/tools/compiler_main.wy` — strict call; `TSTUB name::fn (template: reason)`
    vs `STUB name::fn (reason)`. Both sweep scripts fail on any bare `STUB` line
    (run_corpus_sweep.py counts them into failures; run_selfcompile.py aborts the
    generation).
  - `doc/language-spec.md` — the epic's verbatim `@template` paragraph plus a
    `name::$ast` paragraph (in-module today; cross-module stated as end state).
  - `wy/wyrm/ast.wy` — `template -> (tree)` row: produced by decoration, never by
    parsing, so parser-truth checks vs pypoc's sexpr.py are unaffected.
  - Golden fixture `test/bytecode/template.{wy,wy_a,wyc,out}` + manifest row; source
    checked in beside the artifacts (precedent: `bytes_header/cvm_header.wy`) because
    this repo authors it; a copy must exist at `pypoc/test/bytecode/template.wy` for
    the sweep/build_corpus (do NOT run bare `build_corpus.py` to refresh it — see
    Deviations).

## Deviations from the epic file

- **`::$ast` has no grammar of its own (user decision, supersedes M0 item 3's
  "confirm/complete x::$ast (pypoc AstRef)" framing).** `$` is a legal identifier
  character, so `x::$ast` already parsed — as `'qualified_name` with leaf segment
  `$ast`. The parser and `qualified_name` are untouched; the lowering decides by that
  leaf spelling in `_scope`. Trees are saved/created only where a module actually
  references `::$ast`. No `'astref` node exists.
- **Latent bug fixed as a consequence:** under the port, every `X::$ast` in _dsl.wy
  lowered as an ordinary qualified name → `declare_free_global("X::$ast")` → a
  never-filled global that would fault if the DSL machinery ever ran under the port
  (it never had: nothing executes `_decode_*` before M3). pypoc's compiler_bc resolved
  via `module.definitions` all along; the port now matches. Pinned by
  `test_compiler_templates.wy` ("astref creates no free global for the spelling").
- **Trap message granularity:** M0 says a stubbed template "traps with a message naming
  the template and the recorded reason". `trap`'s operand is a fixed code (C VM maps it
  via `trap_fault_value_f`, src/vm.c:570); carrying arbitrary text would have diverged
  the trap encoding from pypoc's stub (code 0) and broken gen1==gen2. So the runtime
  message stays the fixed trap-0 string; the *recorded reason* lives where it is
  reportable — the `TSTUB` compile-time line and the image's `unlowered` entries.
- **`build_corpus.py` must not be re-run wholesale.** Regenerating the corpus from this
  pypoc checkout drops epic-10 fixtures whose sources aren't upstream
  (`two_module/shapes*`, `dunder_name*`, `bytes_header/*`, `embedded/range.wy`) and
  clobbers the curated DIVERGES/notes columns. `template.*` artifacts were committed
  from its output, then everything else was `git checkout`-restored and the manifest
  row re-added by hand. Refresh the fixture by compiling just that file.
- **Amalgam/self-compile source lists grew:** `predefined.wy` is in
  `AMALGAM_FILES` (before module.wy) and `SELF_SOURCES` — 21 → 22 self-sources. Its
  helpers use `_pd_`-prefixed names for the amalgam's flat namespace.
- **D1/D8/D9/D10 revised mid-session** (epic file + new
  `doc-llm/addendum-decorator-expansion.md`: D1 final = imports-only; D8 = isolated
  single-level expansion VM; D9 = arguments are forms; D6 retired). M0 predates
  expansion and is unaffected: the pre-pass here is exactly what M3's "scope first,
  predefined `template` as fallback" lookup replaces. None of the M0 code assumes
  D1(b)'s dropped same-file-definitions visibility.

## Tests

- before: `meson test` 8/8; corpus sweep 20 fixtures / 19 matches / 0 unexpected;
  selfcompile 23 images, byte-for-byte; wy-tests 6/6.
- after: `meson test` 8/8 (+2 doctest cases inside test_cwyrm: template,
  template gcstress); corpus sweep 21 / 20 / 0; selfcompile 24 images, byte-for-byte,
  strict (expected TSTUB set only); wy-tests 7/7 (+12 template checks).
- No C VM source changed in M0 (`src/`, `include/` untouched) — the milestone is
  entirely compiler-side wy, scripts, fixtures and docs. The "size of the exec-native
  change" ledger item: zero until M1/M2.

## Open questions and known gaps

- **Engine duality of decorator lists (trap for M3's pass-writers):** `many(decorator)`'s
  product walks as a pair list under the tree-walking interpreter but is a native list
  on the C VM (`[]`+`append` shapes differ). `predefined.wy`'s `_pd_last_of`/
  `_pd_rewrite` handle both; anything else that walks `decorated`'s field 1 must too.
- `T::$ast` in type position (`x: T::$ast`) is not refused (type atoms don't go through
  `_scope`); pypoc parse-errors there. Pathological; left loose.
- The pre-pass also rewrites template sites inside quoted `$[...]` data (it cannot tell
  quoted code from live code). Harmless today, recorded in predefined.wy's header.
- `@template` on a `class` compiles the class with no tolerant marking (classes have no
  stub path to mark); a `'template`-wrapped plain statement compiles as written.
  Both deliberate per D7/M0; revisit if block-form templates land.
- Byte-identity vs pypoc's `parser.wyc` (`--strip`) — the epic's report ledger item —
  is still pending M5: parser.wy remains pypoc-built in every generation
  (`run_selfcompile.py`'s INTERIM PROVENANCE DELTA). The *self*-compile fixed point
  (gen1 == gen2, byte-for-byte) held under strict mode with templates marked.
- `_shadowed`-style stubs (`this` in a plain fn under `@template`) exist in both
  engines' images for the golden fixture but are never called there; calling one traps
  (verified manually on both engines, exit 1 after printing the pre-call line).

## Proposed edits to epic_(N+1).md

- None yet (epic 11 untouched). For this epic's remaining milestones:
  - M1 should note `sexpr`/`tree_box` do not exist on the C VM today; the golden
    fixture deliberately avoids them (see `template.wy`'s `describe`/`is nil` trick).
    After M1 the fixture could inspect the tree portably; not worth re-cutting.
  - M3's lookup replaces `expand_predefined_decorators` call site in `compile_module`
    and must produce `_collect_definitions`' post-expansion trees; the index's
    "wrapper removed" comment in module.wy marks the seam.
  - M5's parser.wy bring-up inherits the TSTUB/STUB contract: `@accept` expansion
    stubs in parser.wy must be gone (M3), else the sweep fails on bare STUB lines —
    that is the enforcement epic M0 item 7 asked for.

## Orientation for the next session

- Baseline to protect: `meson test` 8/8, corpus sweep 21/20 matches 0 unexpected,
  selfcompile gen1==gen2 byte-for-byte with only `_tmpl_bool`/`_tmpl_opt` TSTUBs,
  wy-tests 7/7.
- Strict lowering is the default now: any new unlowerable construct refuses modules
  unless it is under `@template`. If a fixture starts REFUSING, check for a bare STUB
  line in the driver output first — the scripts fail on it by design.
- The `'template` wrapper flows: pre-pass (predefined.wy) → toplevel handler
  (module.wy `_template_toplevel`) or statement handler (statements.wy
  `_template_stmt`) → `tolerant=true` down `compile_*` → `FnContext.tolerant` →
  `_compile_or_stub`. Nothing else may set `tolerant`.
- To run the new golden fixture by hand:
  `./buildDir/src/wyrm/wyrm -Itest/bytecode test/bytecode/template.wyc`.
- To refresh `template.*` artifacts, compile only that file with
  `pypoc/.venv/bin/wyrm -Iwy --build-bc pypoc/test/bytecode/template.wy` and copy the
  `.wyc`/`.wy_a` beside the checked-in source; `.out` comes from the walker
  (`pypoc/.venv/bin/wyrm` on the source). Do not run `build_corpus.py` bare (see
  Deviations).
- The mid-session plan revision (D1 final imports-only, D8 isolated child VM, D9
  form-arguments, D6 retired) is already in `epic_10a.md` +
  `doc-llm/addendum-decorator-expansion.md`; read those before M1, they supersede the
  original Goal/D1 text in earlier reports' quotes.
