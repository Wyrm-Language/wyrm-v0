# Epic 10a — Eval context and macro expansion (decorators)

Inserted 2026-09-18, between epic 10 (compiler port) and epic 11 (bootstrap integration),
after epic 10 M7 found that the ported compiler cannot compile `wy/wyrm/parser.wy` (its 86
`@accept` decorators). Numbered `10a` so epics 11/12 and the memory/report references to them
keep their names.

## Goal

Make decorators real compile-time macros, the way Lisp does it, in the self-hosted compiler
running on the C VM. After parsing a module, the compiler creates a **dynamic expansion
context** (a live scope inside the same `wy_context`), loads what the module imports into it,
and runs each decorator (`fn [TreeBase] name(...)` messages) against the decorated tree in
that context, replacing the tree with the decorator's answer until no decorator remains.
Only then does lowering run. The mechanism is a small **eval primitive in the C API,
exposed to wyrm**; the compiler stays in wyrm.

The Python POC's version (`compiler_bc/module.py::_expand_decorators` calling
`wyrm_eval_parse_tree.expand_decorators`) is the reference for *behaviour*, not for
*structure*: it exists only inside pypoc's tree-walking interpreter, executes only top-level
imports, and cannot see definitions from the module being compiled. Where they disagree the
Lisp model here wins (decision D1 below).

**Exit criterion:**
```sh
# the decorators fixture and parser.wy compile through the port, with no pypoc in the loop
./buildDir/src/wyrm/wyrm -Iwy <gen1 compiler .wyc> --mirror wy <out> wyrm/parser.wy
python3 scripts/run_corpus_sweep.py      # 20/20 (decorators/decorated no longer refused)
python3 scripts/run_selfcompile.py       # fixed point over ALL 21 self-sources incl. parser.wy
```

## Evaluation of the "eval in the C API" option (2026-09-18)

**Verdict: feasible, and small. Recommended.**

What already exists and is reused unchanged:
- Load/link: `wy_module_load_bytes`, `wy_context_module_register`, `wy_link_import`,
  wildcard/import fill layers, `wy_link_adopt_messages` (imported `fn [T]` messages become
  callable from the importer - exactly what decorator dispatch needs),
  `wy_link_seed_global` (added with the `__name__` work, `bd6039f`; the way to seed
  `macroexpand` and friends into an expansion scope).
- Inline module init: the `IMPORT` opcodes (`src/vm.c` ~846) already push a module's init frame
  on the current fiber without C recursion.
- The ported compiler itself: it turns a tree into image bytes on the C VM today.

What is missing (this epic):
1. **Running a freshly compiled fragment from wyrm code.** The compiler can produce bytes;
   nothing can load and initialise them mid-execution. `wy_module_run_init` /
   `wy_vm_call_sync` are host-only (design rule: no C recursion from natives), so this must
   be an **exec native** that pushes the init frame inline like `IMPORT` does, not a leaf
   native that calls back into the VM.
2. **Dynamic message send by name** (`send(recv, 'name, args...)`) - decorators are looked up
   by the name in `@name(...)`, which is data at expansion time. Another exec native wrapping
   the existing `WY_OP_MSG_VA` dispatch.
3. **Reading a scope's bindings by name** (`module_get`) - leaf native over
   `wy_link_scope_member`.
4. **A real `TreeBase`.** The C VM's is a bare class with no slots (epic 5/M5). Decorators
   receive `this` as a TreeBase box around the tree and call `sexpr(this)`; needs the
   `__tree` slot plus `tree_box(sexpr)` / `sexpr(x)` builtins. In the wyrm-in-wyrm front end the
   tree already *is* the sexpr (parser.wy emits pair lists), so both are trivial.

Rejected alternative: a general "eval source text" in C. C has no compiler; it would either
embed one (circular) or call back into wyrm. The primitive is "evaluate compiled code in a
scope"; source-to-code stays in wyrm.

Cost/risk: moderate. The exec-native/inline-init shape is the only design-sensitive part
(coroutine/fiber trampolines are the precedent: `next`/`send` in `builtins.c`). Everything
else is glue.

## Decisions (D1 needs the user's confirmation before M3)

- **D1 (proposed):** the expansion scope contains (a) the module's imports, executed for real
  and in order, as pypoc does, **plus (b) top-level definitions** (`fn`, `fn [T]`, `class`,
  constant `static`s) that precede the form being expanded, evaluated as they are passed -
  so a decorator defined earlier in the same file works, like `defmacro` earlier in a Lisp
  file. Other top-level statements (calls, prints) are walked but not executed, as in pypoc.
  If the user wants pypoc's stricter imports-only rule, drop (b); nothing else changes.
- **D2:** outside-in expansion with `macroexpand`, matching pypoc: a decorator is handed its
  operand raw and may call `macroexpand(tree)` to force an inner decorator. `macroexpand` is
  seeded into the scope with `wy_link_seed_global`.
- **D3:** no hygiene (same as pypoc); `gensym` is out of scope.
- **D4:** expansion is deterministic. `_dsl.wy`'s `_next_accept_site_id` counter fixes packrat
  site ids at expansion time, so expansion order must be fixed (source order, outside-in) or
  the self-compile fixed point will not converge.
- **D5:** expansion errors are compile errors that name the decorator and the tree, not VM
  faults. Eval failure inside the scope returns an error value to the wyrm caller.
- **D6:** fragments are ordinary modules named `__expand__::N`, with `__name__` set to that;
  each wildcard-imports the previous fragment plus the module's imports. No mutable/growing
  module is introduced.
- **D7 (`@template`, decided 2026-09-18):** `@template` is a *predefined, shadowable*
  decorator, not a reserved name. Its default implementation wraps the operand tree as
  `$['template, <tree>]`; the compiler's special behaviour is keyed on that **`'template`
  node**, not on the spelling, so a user-defined `template` decorator that returns a
  `'template` node behaves the same. A `'template` tree compiles **exactly as it would
  unwrapped, except** that a failure to lower (which would otherwise be a compile diagnostic
  and, un-stubbed, a known runtime error) emits the trap stub carrying the reason, with no
  diagnostic. Everything not inside a `'template` lowers strictly. `name::$ast` yields the
  tree with the wrapper removed and other decorators already expanded. `::$ast` across
  modules is the desired end state (works when the image retains the tree; a compiled image
  without it errors "tree not available") but is NOT part of this epic. Block/expression
  forms (`@template do: ...`, `foo = @template do { ... }`) are the intended generalisation:
  the wrapper node and the compiler's tolerant-scope flag are general, only `fn` is
  implemented here.

## State-scan checklist

0. M0 starts only after epic 10 M7 has committed (it edits `module.wy`, `compiler_main.wy`).
   First confirm whether the wyrm parser already produces `x::$ast` (pypoc's `AstRef`, see
   `sexpr.py`'s row) and whether `ast.wy` has the node; see M0.
1. Re-run `python3 scripts/run_selfcompile.py`; epic 10 M7 (rescoped) has added stub reporting,
   so read its expected-stub list. After M0 the only allowed stubs are `@template`s and the only
   refusals are `'decorated` nodes in `parser.wy`.
2. Confirm `_dsl.wy`'s `fn $name` template functions and `[TreeBase] accept`/`expect` compile
   and *run* on the C VM (epic 10 report: 21 self-sources compile; nothing has yet executed
   the templates).
3. Read `wyrm_eval_parse_tree.py` `expand_decorated`, `_decorated_is_statement`,
   `sexpr_value`, `macroexpand_value` (lines ~3038-3121) for the exact contract: what a
   decorator receives, how its answer is decoded, statement vs expression position.
4. `grep -n "decorat" wy/wyrm/parser.wy` for the sexpr shape: `$['decorated, decs, stmt]`,
   `$['decorator, name, args]`.
5. Read how `WY_OP_IMPORT` pushes init inline (`src/vm.c` ~846) and how `next`/`send`
   (`builtins.c`) switch fibers; both are the templates for the exec natives.
6. Read the `decorators` corpus fixture and `test/wy/test_dsl.wy` (what already works under
   the tree-walker).

## Milestones

### M0 - Templates: `@template`, `::$ast`, strict lowering (parser + compiler refactor)

Fixes the confusion that stub-everything (`stub_unlowered=true`) hides: some functions exist
only so their tree can be quoted (`_dsl.wy`'s templates: they use `this`, their parameters
are substitution holes, their free names resolve where they are expanded) and are never
called, so they cannot lower. See D7 for the semantics. The 57 `$_mk_*` functions in
`parser.wy` are **not** templates (they are called at parse time); do not mark them, and do
not treat a `$` prefix as meaningful.

**Scope.**
1. *Spec* (`doc/language-spec.md`, Decorators section): add the `@template` paragraph (below)
   and document `name::$ast` (in-module today, cross-module as the stated end state).
2. *Node table*: `'template` (`$['template, tree]`) in `wy/wyrm/ast.wy` and the sexpr docs.
   It is produced by decoration, never by parsing, so parser-truth checks against pypoc's
   `sexpr.py` are unaffected (pypoc's `@template` is a pass-through and never emits it).
3. *Parser*: confirm/complete `x::$ast` (pypoc `AstRef`: `obj` must be a name, field `ast`, any
   other `$field` a parse error). Only if the scan finds it missing.
4. *Predefined-decorator pre-pass* (new `wy/wyrm/compiler/predefined.wy`, called first by
   `compile_module`): rewrites `@template X` to `$['template, X]` for the sole or innermost
   decorator, leaves any other decorator untouched (still refused until M3). No shadow check
   in M0 (there is no eval context yet); M3 replaces this pre-pass with a real lookup:
   scope first, this built-in as the fallback, so shadowing works.
5. *Compiler*:
   - a `'template` handler that lowers its operand with a `tolerant` flag set on the frame
     context (`context.wy`); `functions.wy`'s `_compile_or_stub` honours the flag per
     definition, recording the reason in the stub. The flag is general (a future block
     handler will consult it) but only `fn`/`co` definitions consult it now.
   - **strict by default**: `compile_module`'s `stub_unlowered` default becomes false and
     `compiler_main` stops passing true. A lowering failure outside a template is a compile
     error.
   - `::$ast` lowering: the module's declare pass records each definition's tree (post-
     expansion, wrapper removed); `'astref` lowers to a static tree constant, using the same
     tree-constant path `$[...]` literals use. Errors: unknown name, non-name operand, and
     "not defined in this module" for cross-module (with the wording pointing at the
     end-state rule).
   - calling a stubbed template traps with a message naming the template and the recorded
     reason.
6. *Migration*: `@template` on the 14 `_dsl.wy` templates (12 `$`-prefixed plus `_tmpl_bool`,
   `_tmpl_opt`; pypoc already accepts it, `eae6cc8`). Nothing in `parser.wy`.
7. *Self-compile assertion*: `run_selfcompile.py`/`run_corpus_sweep.py` fail on any stub that
   is not a template; the only other allowed refusal is a `'decorated` node in `parser.wy`
   pending M3.

**Spec paragraph (Decorators section):**

> **`@template`.** `@template` is a predefined decorator marking its operand as a
> *template*: a tree written to be quoted, whose code may never be run. It may be shadowed
> like any decorator. A template is compiled exactly as the same tree would be without the
> decoration, except that if it cannot be lowered, no diagnostic is issued and the code is
> replaced by a trap that raises the recorded reason if it is executed. `name::$ast` yields
> the tree of a definition, with any `@template` marking removed and after other decorators
> have expanded. It is available for definitions in the same module; a compiled module
> supplies a tree only if it retained one. Everything not marked is lowered strictly.

**Files.** Edit: `doc/language-spec.md`, `wy/wyrm/ast.wy`, `wy/wyrm/compiler/{context,
expressions,functions,module}.wy`, `wy/wyrm/tools/compiler_main.wy`, `wy/wyrm/_dsl.wy`
(markers only), the two scripts, and `wy/wyrm/parser.wy` only if the scan says `::$ast` is
missing. New: `wy/wyrm/compiler/predefined.wy`, `test/wy/test_compiler_templates.wy`, a
pypoc-built golden fixture `test/bytecode/template.wy` (its `.out` is pypoc's; both engines
must match at run time).

**Acceptance.**
- A `@template fn` using `this` compiles through the port, and calling it traps with the
  reason; the same function unmarked is a compile error.
- `g::$ast` returns the definition's tree on the C VM; the golden fixture matches on both
  engines.
- `python3 scripts/run_selfcompile.py` compiles all 21 self-sources strictly, and the only
  refusals are `'decorated` nodes in `parser.wy`; `meson test` green.

**Model.** Sonnet for parser/spec/marker work; Opus for the tolerant-scope design in
`functions.wy`/`context.wy` (the flag must not leak into nested non-template definitions
or swallow errors outside the template).

**Fan-out.** Two agents with disjoint files: (a) spec + `ast.wy` + parser + `_dsl.wy` markers;
(b) `predefined.wy` + compiler + scripts + tests. Agent (b) lands first; (a) marks templates
only after (b)'s handler exists.

**Out of scope (designed for, not built):** cross-module `$ast` (an optional image section
of sexprs, dropped by `--strip`); block/expression forms of `@template`; shadow lookup (M3).

### M1 - `TreeBase` slot, `tree_box`/`sexpr` builtins, dynamic `send` (Opus)

`TreeBase` gets its `__tree` slot; add `tree_box`, `sexpr` (leaf); add `send(recv, name,
args...)` as an exec native over the `MSG_VA` dispatch path, looking up the message in the
receiver-visible module tables. Doctests plus a golden fixture: a pypoc-compiled module that
defines `fn [TreeBase] twice()` and a driver that boxes a tree and sends `twice`.

Acceptance: the fixture prints the rewritten tree; `meson test` green.

### M2 - `eval_module`, `module_get`, `std::eval` (Sonnet; may fan out with M1's doctests)

Exec native `eval_module(image: bytes, name: str, parent: module | nil) -> module`: load,
register, link (wildcard from `parent`), push init inline, answer the module. Leaf natives
`module_get(m, name)` / `module_has(m, name)`. Expose all three plus `send` and `tree_box`/
`sexpr` as `std::eval` (hosted) so the compiler and user macros import one place. Errors
come back as error values.

Acceptance: golden fixture reads a `.wyc` from disk into `bytes`, `eval_module`s it, reads a
global through `module_get`, and calls a function from it; gcstress suite included.

### M3 - `wy/wyrm/compiler/expand.wy` (Opus)

The expansion pass per D1-D6: ordered top-level walk, scope construction (imports, then
definitions as fragments compiled through `compile_module` and loaded with `eval_module`),
decorator application (evaluate arguments as a fragment expression, box, `send`, decode,
repeat), nested/expression-position decorators, `macroexpand`. Also unstubs
`compiler_main.wy`'s reporting (scan item 1).

Acceptance: `decorators/decorated` compiles through the port and matches its `.out`.

### M4 - Wire into `compile_module`; corpus and `_dsl` green (Sonnet)

`compile_module` runs expansion first when the tree contains `'decorated` (skip otherwise,
like pypoc, so decorator-free modules pay nothing). `test/wy/test_dsl.wy` and one negative
test per D5 error category. `run_corpus_sweep.py` 20/20 (drop the REFUSED entry).

### M5 - True self-compile fixed point (Opus)

Compile all 21 self-sources including `parser.wy` (86 `@accept` sites) with the port; gen2
== gen3 byte-identical (gen1 == gen2 recorded if free). Retire epic 10's interim
"pypoc builds `parser.wyc`" arrangement. Wire `run_corpus_sweep.py` and `run_selfcompile.py`
into `meson test` (skip without `pypoc/.venv` until epic 11 lifts that).

## Risks

- **Exec-native inline init is the one design-sensitive piece** (fiber state while a module
  init frame is pushed under a native). Mitigation: M1 is Opus, copies the `IMPORT`/`next`
  precedents, and gcstress-runs the golden fixtures.
- **Scope leakage.** Fragment modules stay registered in the context. Mitigation: name them
  under `__expand__::`, and discard the scope's registrations when a compile finishes (add
  `wy_context_module_unregister` if the scan finds none).
- **Non-determinism breaks the fixed point** (D4). Mitigation: a dedicated test that expands the same
  module twice in one process and in two processes and diffs the trees.
- **Same-module definitions have side effects** if they read module state at definition time.
  Mitigation: D1 evaluates only definition forms; document the rule in `doc/language-spec.md`'s
  Decorators section (the spec currently says only "the compiler calls the defined decorator
  function").
- **Compile-time execution of arbitrary code** (same trust model as pypoc). Recorded; no
  sandbox this epic.

## Out of scope

Hygiene/gensym; cross-module `::$ast` and block-form `@template` (see D7/M0); a REPL; runtime `eval` of source text in user programs; caching expansion
results; changing decorator authoring syntax.

## Report

`vm_plan/epic_10a_report.md` per README template. Record: D1's final wording as implemented,
whether byte-identity vs pypoc's `parser.wyc` (`--strip`) held or which sections differ and
why, and the size of the exec-native change.
