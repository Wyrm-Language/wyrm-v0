# Notes for automated agents: traps, method, and past regressions

Distilled from epics 7-10 (see `doc-llm/history/wypoc-vm-port/epic_N_report.md` for full detail). Read this before
touching `src/vm*.c`, `src/embed/wyrm/*.wy`, or the self-hosted compiler.

## Engines and which one is the reference

| Engine | Role |
|---|---|
| pypoc tree-walker (`wyrm_eval_parse_tree.py`) | **Semantic reference** (stays long term). Where engines disagree, match this. |
| pypoc bytecode compiler + VM | Being retired; bug-compatible only where cheap. |
| This repo's C VM (`buildDir/src/wyrm/wyrm`) | Compiles and runs `.wy` in-process (epic 11: builtin module table + `__wycache__` .wyd cache); runs `.wyc`/`.wyd` directly. Self-sufficient - no Python. |
| Ported compiler (`src/embed/wyrm/compiler/*.wy`) | Runs on the C VM, compiled by pypoc (gen0) or by itself (gen1+). |

- Always use `pypoc/.venv/bin/wyrm`, never `~/tools/bin/wyrm` (stale checkout).
- Bar for the compiler port is *functional*, not byte-identical with pypoc.
- The spec is silent on some semantics (e.g. `==` on pairs/lists/dicts). "Structural" is
  inherited from pypoc, not written down. Check `doc/language-spec.md` before asserting.

## Generations of the self-hosted compiler

- **gen0 / stage0** = the compiler images embedded in the binary (`src/embed/**.c`,
  committed). pypoc is no longer involved. (History: gen0 used to be a pypoc-compiled
  *amalgam* of all compiler modules concatenated into one module; that driver and
  `run_corpus_sweep.py` are gone.)
- **gen1** = stage0 compiling the current `src/embed/` sources into separate `.wyd` modules
  (`compiler_main.wy` run in-process by the binary). Output tree first on the import path,
  so decorator expansion loads this compiler's own tree shapes.
- **gen2** = gen1 compiling them again. Bar: gen1 == gen2 byte-for-byte, then the fresh
  tree's embedded-table emit == committed stage0 (`scripts/run_selfcompile.py`;
  `SELFCOMPILE_KEEP=<dir>` keeps the trees). After a compiler change, regenerate stage0
  with `scripts/regen_builtins.py`.
- The notes below about the amalgam describe historical gen0 bugs; the failure modes are
  still a useful checklist when a gen1 image misbehaves.

**The amalgam hides bugs.** One shared namespace means: same-name top-level helpers in
different modules silently shadow each other (last wins), and transitive names (a name
reachable only through another module's wildcard import) always resolve. Real module trees
break both. Wildcard imports ARE re-exported now (design/modules.md M2: a module exports
what it imports), but still import what you use: two wildcards that reach different
bindings of one name (a duplicated private helper, or one module loaded twice under two
spellings, as `opcodes` and `wyrm::opcodes` were) make that name ambiguous where it is read.
Keep compiler-side helper names distinct from front-end ones (`_path_char`, `_join_path`,
`_pool_kind` exist for this reason).

## Debugging recipes that worked

1. **Bisect by swapping images.** Copy a gen1 tree, replace ONE `.wyc` with the pypoc- or
   gen0-built equivalent, rerun the failing command. Then swap all. If only the all-swap
   passes, suspect module linking; if one swap fixes it, that module is miscompiled.
2. **Diff opcodes, not bytes.** `wyrm --disasm a.wyc | awk '{print $2}'` vs the same for the
   pypoc build of the same source; `diff` shows the first miscompiled construct. Operand
   columns differ legitimately (slot numbering); opcode sequences should match.
3. **Shrink the failing input.** Feed one-line sources to the gen1 driver
   (`printf 'x := "a"\n' > f.wy`); five minutes of one-liners beat reading 3000 lines.
4. **Instrument by recompiling a scratch copy** of one module with `println`s, compile it with
   the current driver, drop the `.wyc` into a copy of the tree.
5. **Read the error's layer.** `native call failed: len/str` usually means a *wrong-typed value
   arrived* (miscompiled caller, shifted registers), not that `len`/`str` is broken.

Symptom -> usual cause seen so far:

| Symptom | Cause |
|---|---|
| `unreachable code reached`, `value is not callable` | function was stubbed, or a parse glue turned a statement into a call |
| `undefined name 'x'` at compile time | nothing the module imports offers `x` (M2 places every name before running) - import the module that defines it |
| `ambiguous name 'x'` fault | two wildcards reach two different bindings of `x`; import it explicitly, or drop a duplicate |
| every string literal comes out with backslashes | `decode_str` shadowed in the amalgam |
| header/section offsets wrong in output image | operator precedence in the parser (`a + b * c`) |
| argument arrives as the receiver object | receiver temporaries not freed before message args |
| works in gen0, fails in gen1 | amalgam-only name resolution (see above) |

## Parser pitfalls (src/embed/wyrm/parser.wy)

- Grammar is packrat with `@accept`; block-form `if/while/for` are *primaries*. A block ends
  at DEDENT with no NEWLINE token, so anything postfix (`(`, `[`) at the start of the next
  line would chain onto it. Block primaries take no postfix ops (fixed); keep it that way.
- `list_expression` discards a trailing comma. `paren_expression` keeps it (`(x,)` is a
  1-tuple). Do not reuse `tuple_expression` inside parens.
- `binary_expr` is a *flat* grammar rule; precedence lives in `$_binary_expr`
  (tiers `|` < `^` < `&` < shifts < `+ -` < `* / %`). Any new binary operator needs a tier.
- Sexpr conventions: `nil/true/false/break/continue/pass` and empty literals are bare symbols,
  not `$[kind]` nodes. `recv ! m()` (`'send`, args nil) differs from `recv ! m` (`'bound`).
- Parser tests must include mixed-operator, trailing-comma and block-then-`(` cases; earlier
  suites did not and three bugs shipped.
- `pos` fields do not exist on src/embed/wyrm nodes; error messages have no source positions.

## C VM traps

- **Two-switch trap:** `wy_op_eq` (`include/wyrm/op.h`) and `compare_f` (`src/vm_ops.c`) are
  separate type switches. Change both for any new type. Same class: `wy_vm_is_f` type-name
  list (`is bytes`, `is sym` were each missing once).
- `==` on aggregates is pointer identity on the C VM but structural in both pypoc engines.
  The compiler uses `same_node` (analysis.wy) to avoid relying on it. Open VM follow-up.
- Sentinels: nil, Unset, and error are distinct. `wy_value_is_error` must be true for Unset
  (matches pypoc). The dispatch wildcard is **nil**, not Unset. When a spec names a sentinel,
  grep for the literal word.
- Method/message bodies: receivers occupy `P0..P(t-1)` outside the declared params; frame
  pushers must use the `this_count`/`this_values` pair, not prepend to args.
- Keyword args reach `push_bytecode_call_bind_f` with **string** dict keys, not symbols.
- `!` messages on primitives: register through `install_native_messages_`
  (`src/builtin/builtins.c`); do not add a second mechanism.
- Imported `fn [T]` messages are adopted at import time (`wy_link_adopt_messages`).
- Natives must not use `wy_vm_call_sync` (no C recursion).
- `int(str)` reads an integer literal like pypoc's `int(x, 0)`: `0x`/`0o`/`0b`, `_` separators,
  no implicit octal (`010` is an error). Out of `wy_word` range is an error, not a clamp.
- Indexing a pair list walks the cdr chain (`$[a, b][1]` is `b`), for reads and `p[i] = v`.
- `f(x, *rest)` in `call_va` is a known, unfixed gap. (`range` is now a native iterator; the old
  coroutine-prelude gap where `for i in range(...)` never iterated is gone.)

## Codegen pitfalls (shared by pypoc and the port)

- Message/call windows need receiver at `window`, args at `window+1...`. Any receiver
  expression that leaves temporaries above `window` shifts every argument. pypoc avoids it
  only when the receiver is parenthesized (parsed as a 1-tuple); an unparenthesized
  `a.b.c ! m(x)` or a module-global receiver still miscompiles under pypoc.
- Function names must be declared up front at module level (recursion, forward references).
- `push_block` scope matching must be structural (`same_node`); positional fallback was wrong.
- Stub semantics: `stub_unlowered=true` turns an unlowerable body into a trapping stub with a
  recorded reason. Stubs are silent unless `compiler_main` prints them (it does: `STUB ...`).
  A new unexpected stub is a bug; the expected set is decorators and `_dsl` templates.

## wy authoring gotchas (for files in `src/embed/` and `wy/`)

- Dict literals must fit on one physical line. Default parameter values must be literals
  (use `x: list | nil = nil`). `fn`/`f` cannot be identifiers.
- `len(x)` and indexing errors: out-of-range indexing is a *catchable* error.
- A line starting with `(` after a block is now a new statement; older notes saying to avoid
  it (or bind through a local) describe the pre-fix parser.
- These compiler `.wy` files run reliably only on the C VM, not pypoc's interpreter
  (`opcodes.wy` uses constructs the interpreter cannot run). Use the drivers under
  `wy/wyrm/tools/` and the amalgam pattern; pypoc `--check`/`--build-bc` is fine for syntax.

## Process lessons

- The epic protocol is scan -> execute -> report; the report is the only hand-off. Update the
  report when a blocker's diagnosis changes (the "glue after `for`" note was wrong for months:
  it was any block).
- Delegating whole epics to weaker agents produced green-looking reports over latent bugs
  (flat precedence, missing trailing comma). Insist on: full-fixture runs, mixed-construct
  tests, and checking the *unexpected* stub/diff list, not only the pass count.
- Be suspicious of "workaround applied" notes. Each one in epic 10 (parenthesized receivers,
  hand-written while loops, local temps) was hiding a parser or codegen bug that later
  mattered. Record the root cause or fix it.
- Fan-out to parallel subagents works when each gets exact signatures and a "files you must
  not touch" list.
- Tests to run before declaring done: `meson test -C buildDir` (includes the `compiler` suite:
  behavior corpus and self-compile fixed point). Slow one alone:
  `meson test -C buildDir --suite compiler`. Prefer a release tree (AGENTS.md).

## Decorator expansion (epic 10a)

- Decorators are expanded in a **throwaway VM** (`std::expand::expand`,
  `src/platform/hosted/expand_native.c`); the expander is `src/embed/wyrm/compiler/expand.wy`. The child
  reuses the parent's import hook *including the builtin module table* (epic 11), so the
  expander and the whole compiler tier resolve with no file on disk; decorator modules the
  compiled module imports resolve from `-I` roots (`.wy` sources compile through the .wyd
  cache on a scratch machine; `.wyc`/`.wyd` load directly). The scope module holds *all* of a
  module's imports, so a module with decorators cannot import `std::io` (or any host module).
  `std::io` is an embedded module now, so this is enforced by the expansion child's import hook
  (`expand_child_import_` refuses `std::io`) and by the child having no `__open`/... natives;
  `std::expand` stays a host module absent from the builtin table.
- C code holding values across VM runs in ANY context must root them
  (`wy_context_root_push_f`): module init runs GC safepoints. An unrooted copied-in tree was
  freed mid-expansion (5000-element list came back length 1).
- **Two tree shapes exist.** pypoc's (`'fn`, `$['name, x]`, `'msg`) and the port's (`'fn_def`,
  bare-symbol names, `'message`, pair lists). `ast.wy` owns the difference (`WY_SHAPE`, detected from
  a probe template, so it follows the compiler that built the module); `_dsl.wy` must only read and
  build trees through its `s_*`/`mk_*` helpers. A raw `$['name, x]` in `_dsl.wy` is a bug.
- `range(a, b)` is a native iterator (an `ITER` value; `for` and `next()` accept it). Older
  `.wy` written around the former zero-iteration gap (e.g. `std::pairs::list_tail`'s `while`)
  can go back to `for`. `fn $name` is an ordinary function, not a TreeBase method.
- Builtins the C VM adds (`tree_box`, `bind_message`, `error_message`) are unknown to pypoc
  and must be listed in `compiler/context.wy` BUILTIN_NAMES for the port. pypoc cannot compile
  a module that uses them; fixtures that do are compiled by the port and have hand-written
  `.out`s (`test/corpus/expand/README.md`).
- `f(*xs)` passes a native list positional; the VM accepts list or tuple (was tuple-only).
- Function-level `import` is not supported by the bytecode compiler; imports are top-level.
- Fast loop for compiler-side changes: copy a `SELFCOMPILE_KEEP` gen1 tree, recompile single
  modules into it with its own `compiler_main.wyc --mirror wy <tree> <rel.wy>`, then compile
  the module under test against it (seconds instead of a ~7 min self-compile).

## REPL / session traps (found while building it)

- `slot` is a keyword: a variable named `slot` in wy source is a parse failure that surfaces only
  as "compile_module needs a 'module node". Same for a statement starting with `(` right after
  another expression (it glues on as a call): use a helper function or a local.
- The compiler is the same one for scripts and sessions; a session is switched on by
  `ModuleContext.session_declared` being non-nil. Anything you add to name resolution must keep the
  ordinary path (nil) byte-identical: check `git status src/embed` after a regen, only the modules
  you edited may change.
- A delta's exports/free tables list only *new* names; a shadowing name replaces its slot
  (`wy_slot_dict_set`), and a filled forward slot is exported although it predates the delta.
- Free names filled by an import (`std::io::println`) are filled when the import runs, so a session
  re-runs the fills after every extend (`wy_session_refill_`). If a qualified name in a later input
  is unbound, look there first.
- The REPL compiles inside the collected VM, so GC-stress runs of the differential are slow; keep
  those to a small subset.

## Performance experiments that were tried and not kept

Record of optimizations that were implemented, measured and reverted, so they are not redone.
Numbers are wall-clock seconds from release builds (`-Dbuildtype=release -Db_ndebug=true`), 3 runs
each, benchmarks from `Programming-Language-Benchmarks/bench/algorithm/*/1.wy`.

### Computed-goto (threaded) dispatch in `wy_vm_run` - no gain, reverted

**What was tried.** GNU labels-as-values dispatch: `WY_JUMP_TABLE_*` / `WY_DISPATCH_*` macros in
`include/wyrm/sys/toolchain.h` (a 256-entry `static const void* const` table, one label per `case`,
`goto *table[op]` at the end of each handler) with a fallback to the plain `switch` when
the extension was unavailable or `WY_DISABLE_COMPUTED_GOTO` was defined. The decode block at the top of
the loop became a macro so each handler could re-run it and jump directly to the next handler. All 14
meson suites passed in both modes; the change was semantically sound.

**Why it was dropped.** No measurable speedup, and it is non-standard C (`-Wpedantic` needs pragmas, the
`[0 ... 255]` range designator and `goto *` are GNU extensions), which is not worth carrying for nothing.

| benchmark | plain `switch` | computed goto (default flags) |
|---|---|---|
| fannkuch-redux 10 | 8.0 - 8.2 | 8.2 - 8.4 |
| lru 100 500000 | 2.5 | 2.6 |
| nsieve 9 | 2.5 | 2.5 |
| binarytrees 14 | 1.9 | 1.9 |

Two tuned variants were also measured (`--param=max-goto-duplication-insns=1000`, alone and with
`-fno-crossjumping`) and were also within noise (about 1-3%) of the plain switch.

**Things learned that still apply:**

- With default GCC flags the compiler undoes the technique: it factors every `goto *` back into one
  shared indirect jump (objdump of `vm.c.o`: 4 indirect jumps versus 2 for the switch). The number of
  jumps only reaches roughly one per handler (105) with `max-goto-duplication-insns` raised and
  `-fno-crossjumping`. When counting, match `jmp +\*`, not `jmp .*\*%r`: indexed indirect jumps print
  as `jmp *(%rax,%rdx,8)`.
- Even with per-handler jumps there was no gain, so dispatch branch prediction is not the bottleneck on
  the current CPUs. Time is in handler bodies (callgrind: `wy_vm_run` 37-56%, `wy_vm_binop_f` 12-17%,
  `push_bytecode_call_bind_f` 15% on binarytrees, dict `find_sparse_slot` 12% on fannkuch).
- Better targets, in the order they were measured: the fixed 64 KB GC threshold
  (`WY_CONTEXT_GC_THRESHOLD_DEFAULT`, which made binarytrees 15 go from 6.5 s to 1.0 s at 64 MB), an inline
  word/word fast path for `ADD`/`SUB`/compare/bit ops in the loop instead of calling `wy_vm_binop_f`, and
  the call path (`push_bytecode_call_bind_f`).
- Retry only if handler bodies get much cheaper first (dispatch overhead is then a larger share), and
  re-measure with per-handler jumps confirmed in the object code.
