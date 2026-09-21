# Epic 10a — Eval context and macro expansion (decorators)

Inserted 2026-09-18, between epic 10 (compiler port) and epic 11 (bootstrap integration),
after epic 10 M7 found that the ported compiler cannot compile `wy/wyrm/parser.wy` (its 86
`@accept` decorators). Numbered `10a` so epics 11/12 and the memory/report references to them
keep their names.

## Goal

Make decorators real compile-time macros, the way Lisp does it, in the self-hosted compiler
running on the C VM. After parsing a module, the compiler hands the tree to a **temporary,
isolated expansion VM** (its own machine, context, heap and module registry - never the
compiling VM's). The child loads what the module imports, normally, from their compiled
images, and runs each decorator (`fn [TreeBase] name(...)` messages) against the decorated
tree, replacing the tree with the decorator's answer until no decorator remains. The
expanded tree is copied back into the parent and the child is destroyed. Only then does
lowering run. The mechanism is a small **`expand` primitive in the C API, exposed to wyrm**;
the compiler and the expander stay in wyrm.

Language-level wording for all of this is in `doc-llm/addendum-decorator-expansion.md`
(proposed spec text; fold into `doc/language-spec.md` when M3 lands).

**The module being compiled is not part of the execution path.** It is still a tree. Only
its imports (already compiled, already expanded) execute, inside the child. Nothing of the
compiling VM's state - globals, fibers, module registry, counters - is visible to a
decorator, and nothing a decorator does can pollute it.

The Python POC's version (`compiler_bc/module.py::_expand_decorators` calling
`wyrm_eval_parse_tree.expand_decorators`) is the reference for *behaviour*, not for
*structure*: it exists only inside pypoc's tree-walking interpreter, executes only top-level
imports, and cannot see definitions from the module being compiled. Behaviour matches
pypoc on that point (decision D1 below); the isolation (D8) is new.

**Exit criterion:**
```sh
# the decorators fixture and parser.wy compile through the port, with no pypoc in the loop
./buildDir/src/wyrm/wyrm -Iwy <gen1 compiler .wyc> --mirror wy <out> wyrm/parser.wy
python3 scripts/run_corpus_sweep.py      # 20/20 (decorators/decorated no longer refused)
python3 scripts/run_selfcompile.py       # fixed point over ALL 21 self-sources incl. parser.wy
```

## Implementation status and deltas (2026-09-20; see epic_10a_report.md)

M0-M5 are implemented (report, "M5 landed": `ast.wy` owns the tree shape). Where the code differs from the text below, the code wins:

- The dynamic-send primitive is a LEAF `bind_message(scope, recv, name)` answering a bound
  message (the VM already calls `bound_msg` values); `send` was taken (coroutines) and no
  exec native was needed. `tree_box`/`sexpr` are leaf builtins; `TreeBase` has a `__tree` slot.
- The expansion primitive is `std::expand::expand(tree, scope_image, entry)` (a hosted module),
  not a builtin `expand` (that name is the list builtin). The child reuses the parent's
  import hook, so the parent does not pre-collect the import closure; the expander and the
  decorator modules must be `.wyc` images on the compile's `-I` path.
- `eval_module`/`module_get`/`std::eval` and the exec-native inline-init item are gone (D9).
- `compile_module` takes an `expander` callback (wired by `compiler_main.wy` to
  `wyrm::compiler::expansion::expand_decorators`) instead of calling `std::expand` itself, so
  module.wy stays importable by pypoc's interpreter.
- Same-module decorator use is refused (D1); `macroexpand` is specified (D2) but NOT yet
  provided, and `sexpr(this)` does not expand nested decorators (no repo decorator nests).
- A lone `@template` is still rewritten by the M0 pre-pass without consulting the scope
  (shadowing is honoured only for the cases the expander sees: outer/inner positions).
- `f(*xs)` (call_va/msg_va) now accepts a native list positional; call_va calls bound messages.

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
0. **The `expand` primitive** (host side, callable from wyrm in the *top-level* VM only):
   `expand(tree, imports) -> tree | error`. `imports` is the transitive closure of the
   module's imports as compiled images, resolved by the parent (the compiler already finds
   `.wyc` files on the include path); the child never reads a file. `expand` creates a fresh
   VM and context flagged as an expansion VM with an **allow-list native table** (D10),
   loads and initialises `imports` **host-side** (`wy_module_load_bytes`, register, link,
   `wy_module_run_init` on the child - host calls on a different VM, so no inline-init exec
   native is needed), copies `tree` in, calls the expander entry (`expand.wy`, an ordinary
   image loaded the same way) with a host sync call on the child, copies the resulting tree
   out, destroys the child. Called inside an expansion VM it returns an error ("expansion
   is not re-entrant"): nesting depth is fixed at 1. Only tree-shaped data crosses: nil,
   bool, numbers, str, symbol, pairs, tuples; anything else returned by a decorator is a D5
   error. The copy is iterative (no recursion).
1. **No code generation in the child.** Under D1/D9 the module being compiled never
   executes and decorator arguments are trees or literal values, so the child needs neither
   the compiler nor `eval_module`/fragment loading. This retires the exec-native inline-init
   piece the earlier plan carried; `send` (below) is the only new exec native.
2. **Dynamic message send by name** (`send(recv, 'name, args...)`) - decorators are looked up
   by the name in `@name(...)`, which is data at expansion time. Another exec native wrapping
   the existing `WY_OP_MSG_VA` dispatch.
3. (Dropped: `module_get` / `eval_module` are no longer needed, see 1.)
4. **A real `TreeBase`.** The C VM's is a bare class with no slots (epic 5/M5). Decorators
   receive `this` as a TreeBase box around the tree and call `sexpr(this)`; needs the
   `__tree` slot plus `tree_box(sexpr)` / `sexpr(x)` builtins. In the wyrm-in-wyrm front end the
   tree already *is* the sexpr (parser.wy emits pair lists), so both are trivial.

Rejected alternatives: a general "eval source text" in C (C has no compiler; it would embed
one or call back into wyrm), and evaluating module code or decorator arguments in the
expansion VM (needs the compiler in the child and an inline-init exec native; unnecessary
under D9).

Cost/risk: moderate. The new pieces are `expand` (VM lifecycle, tree copy, allow-list
natives) and `send` (an exec native; coroutine/fiber trampolines are the precedent:
`next`/`send` in `builtins.c`). Everything else is glue.

## Decisions (D1 and D8 confirmed by the user 2026-09-19)

- **D1 (final 2026-09-19):** the expansion VM contains **only the module's imports**, run
  for real from their compiled images, as pypoc does. The module being compiled never
  executes: a decorator must come from an import, not from a definition earlier in the same
  file. The module's own top-level statements are walked as data only. An explicit
  compile-time marker (Lisp's `eval-when`) is designed-for but out of scope.
- **D9 (2026-09-19): arguments are forms.** `@name(a, b) stmt` passes `a` and `b`
  unevaluated. A pure literal (number, string, symbol, list/tuple literal of literals) is
  delivered as its value, so `@add_value(5)` with `v: int` works; any other argument is
  delivered as a tree (`TreeBase`); a parameter annotated `TreeBase` gets the tree even for a
  literal. Literal folding is a small pure tree evaluator inside `expand.wy`. No compile or
  execution of module code is involved. (M3 detail: the annotation check needs the resolved
  message's parameter types; if that is not introspectable, fall back to "literal -> value,
  else tree" and record it in the report.)
- **D8 (2026-09-19): isolated, single-level expansion VM.** Each module compile creates one
  temporary top-level machine and context, runs all of that module's expansion in it, copies
  the result tree out and destroys it. Never a grandchild: `expand` is illegal inside an
  expansion VM. The expander walks forms in source order and `macroexpand`s a form fully
  before anything is derived from it. The child contains no compiler, so it cannot re-enter
  expansion by accident. Consequences: no leaked registrations, no shared counters (`_next_accept_site_id`
  restarts per module compile - confirm that site ids need only be unique within a module),
  an obvious place to add resource limits later (out of scope here).
- **D2:** outside-in expansion with `macroexpand`, matching pypoc: a decorator is handed its
  operand raw and may call `macroexpand(tree)` to force an inner decorator. `macroexpand` is
  an ordinary wyrm function of the child's expander (it finds `'decorated` nodes, looks the
  decorator up in the child's scope, sends, recurses); it is seeded into the scope with
  `wy_link_seed_global`. Nothing calls back into the parent.
- **D3:** no hygiene (same as pypoc); `gensym` is out of scope.
- **D4:** expansion is deterministic. `_dsl.wy`'s `_next_accept_site_id` counter fixes packrat
  site ids at expansion time, so expansion order must be fixed (source order, outside-in) or
  the self-compile fixed point will not converge.
- **D5:** expansion errors are compile errors that name the decorator and the tree, not VM
  faults. Eval failure inside the scope returns an error value to the wyrm caller.
- **D6 (retired 2026-09-19):** there are no fragment modules (`__expand__::N`); D9 removed
  them. Number kept so references stay valid.
- **D10 (2026-09-19): no ambient authority.** The parent supplies import images; the child's
  native table is an allow-list of pure operations (arithmetic, strings, collections,
  symbols, trees, `send`, `macroexpand` support, `tree_box`/`sexpr`) with **no file,
  network, process, environment, clock or random natives and no I/O modules**. An imported
  module whose init needs a missing native fails to load with a D5 error naming the module
  and the capability. Also serves D4 (determinism) and `hosted=false`. Resource limits
  (memory, steps) are out of scope but the VM-per-module shape allows them.
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
5. Read how `next`/`send` (`builtins.c`) switch fibers (the template for the `send` exec
   native), how `wy_vm`/`wy_context` are created and destroyed from host code (the template
   for `expand`), and how natives are registered into a context (the allow-list, D10).
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

### M2 - `expand` primitive and the expansion native allow-list (Opus)

**`expand(tree, imports) -> tree | error`** per "What is missing" item 0: fresh VM and
context, expansion flag, allow-list native table (D10), host-side load/link/init of the
import images, iterative tree copy-in/copy-out over the allowed data kinds, non-re-entrant
error, child destroyed on every path (success, error, decorator fault).

Acceptance (doctests, plus a tiny golden fixture with a hand-written expander entry):
- copy fidelity (shapes, sharing, long lists, deep nesting) and rejection of non-tree
  results (D5 error naming the decorator);
- the re-entrancy error;
- no registrations, globals or counters visible in the parent afterwards, and a decorator
  library's counter restarts per call;
- an import needing a disallowed native (file/clock/random) fails with the capability named;
- allocator accounting returns to baseline on success and on every error path; gcstress.

Model split: Opus for `expand`; Sonnet for doctests. Standalone (not `std::eval`):
`eval_module`, `module_get` and a `std::eval` module are dropped from this epic.

### M3 - `wy/wyrm/compiler/expand.wy` (Opus)

The expander, per D1-D10, a normal wyrm module run **entirely inside the child VM** with one
entry point called by `expand`: ordered outside-in walk over the tree, decorator lookup by
name in the child's scope (predefined `template` as fallback, so shadowing works, replacing
M0's pre-pass), argument delivery per D9 (literal fold, else tree box), `send`, decode the
answer, repeat until no `'decorated` remains, nested and expression-position decorators,
`macroexpand`. It imports nothing but pure support modules. Also: parent-side wiring that
collects the import closure's images for `expand`; fold `doc-llm/addendum-decorator-expansion.md`
into `doc/language-spec.md`; unstub `compiler_main.wy`'s reporting (scan item 1).

Acceptance: `decorators/decorated` compiles through the port and matches its `.out`; a test
each for: argument as literal value, as tree, `TreeBase`-annotated literal; a decorator
answering a non-tree; a decorator from a module needing I/O (fails naming the capability);
`macroexpand` of an inner decorator.

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

- **`send` as an exec native** is the one fiber-sensitive piece (dispatch under a native).
  Mitigation: copy the `next`/`send` trampoline precedent and gcstress the golden fixture.
- **Allow-list too small or too large (D10).** Too small: a legitimate import (a pure helper
  whose init touches a missing native) fails. Too large: an I/O path leaks in. Mitigation:
  derive the list from what `_dsl.wy` and its imports actually use, add a test that asserts
  the child has none of the I/O natives, and fail loudly, naming the capability.
- **Import closure.** The parent must supply *all* transitive import images, in link order,
  and they must match what the compiled module will link against at run time. Mitigation:
  reuse the compiler's existing import resolution; test a two-level import.
- **Scope leakage** is removed by construction (D8): the expansion VM is destroyed after
  each module compile. Residual risk is `expand`'s teardown on error paths and the tree copy
  (unsupported value kinds, sharing/cycles): covered by M2's acceptance.
- **Non-determinism breaks the fixed point** (D4). Mitigation: a dedicated test that expands the same
  module twice in one process and in two processes and diffs the trees.
- **Decorators must come from imports** (D1). Document the rule in `doc/language-spec.md`'s
  Decorators section (the spec currently says only "the compiler calls the defined decorator
  function"). Check that no self-source defines and uses a decorator in the same file.
- **Compile-time execution of arbitrary code** from imports (same trust model as pypoc), now
  contained by D8 (isolated VM) and D10 (no ambient authority), but with no resource limits
  this epic.

## Out of scope

Hygiene/gensym; cross-module `::$ast` and block-form `@template` (see D7/M0); a REPL;
runtime `eval` of source text in user programs; caching expansion results; changing
decorator authoring syntax; `eval_module`/`module_get`/`std::eval`; an `eval-when`-style
compile-time marker; resource limits on the expansion VM.

## Report

`doc-llm/history/wypoc-vm-port/epic_10a_report.md` per README template. Record: D1's final wording as implemented,
whether byte-identity vs pypoc's `parser.wyc` (`--strip`) held or which sections differ and
why, the size of the `expand` and `send` changes, and how D9's parameter-annotation check was
resolved.
