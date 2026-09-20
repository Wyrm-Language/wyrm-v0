# Addendum: decorator expansion (compile-time macro semantics)

Proposed additions to `doc/language-spec.md`, *Semantics > Decorators*, for merging into the
canonical wyrm spec. They define what the spec currently leaves open: which code a decorator
may run, what it receives, and what the expansion environment may touch. They are the design
of `vm_plan/epic_10a.md`. Each section says what is defined and why.

## The module being compiled does not execute

Expansion is a compile-time step over a *tree*. The module being compiled is never run,
not even in part. Only its imports run, and they run from their compiled images, so they
were already fully expanded when they were built. A decorator therefore comes from an
import; a decorator defined earlier in the same file is not visible to expansion.

Why: the module is not yet a program, so running any of it makes expansion depend on
half-compiled state. Lisp resolves this with `eval-when`; wyrm starts stricter and may add an
explicit opt-in later (see *Reserved*).

## Arguments are forms, not values

`@name(a, b) stmt` calls the decorator message on `stmt`'s tree, passing `a` and `b`
**unevaluated**: as trees, like a Lisp macro's arguments.

- An argument that is a pure literal (number, string, symbol, or a list or tuple literal
  of literals) is delivered as its **value**. This keeps `@add_value(5)` working with
  `fn [ast::BaseTree] add_value(v: int)`.
- Any other argument is delivered as a **tree** (a `TreeBase`). A parameter annotated
  `TreeBase` receives the tree even for a literal.
- Free names in a tree resolve where the tree is finally expanded, not at the decorator
  call. There is no hygiene and no `gensym`.

Why: the argument may name things in the module being compiled, which does not exist yet as
values. Passing forms means nothing in the active module has to run, which is also what
Lisp does. A decorator that wants a value from a non-literal argument computes it from the
tree.

## Operand of an expression statement

A decorator on an expression statement (`@d f(x)` on its own line) receives the expression
`f(x)`, not a statement wrapper; a non-statement answer is placed back in statement position.

## Order and completeness

Expansion is outside-in, in source order. A decorator receives its operand raw: an
inner decorator application is still a `'decorated` node inside it. `sexpr(this)` returns
the operand as it was received, unexpanded. A decorator that needs the inner application
expanded first calls `macroexpand(tree)`, an ordinary function of the expansion
environment (**specified here, not yet provided by wyrm's expansion environment**; no
decorator in the wyrm sources nests, so nothing depends on it yet). Expansion repeats on a
decorator's answer until no `'decorated` remains, and lowering starts only after that. Decorators
that must fire in a fixed order (for example those that number things) therefore see one
deterministic order.

Expansion is deterministic: the same tree and the same imports always produce the same
result. This is a requirement, not an accident, because the self-hosted compiler's output
must reproduce itself byte for byte.

## The expansion environment is isolated

Expansion of one module runs in a **temporary, separate machine and context**, created for
that module and destroyed when expansion finishes. Consequences, all visible to authors:

- A decorator cannot observe or alter the compiling program: no globals, no registry, no
  counters. State it creates (module-level counters in a decorator library, for
  example) starts fresh for each module compiled.
- Only **tree-shaped data** crosses out of it: nil, booleans, numbers, strings, symbols,
  pairs, tuples and lists of those. A decorator that answers anything else (a function,
  an object, a handle) is a compile error naming the decorator.
- Expansion does not nest: a decorator cannot start another expansion environment. Source
  containing decorators is never compiled during expansion; use `macroexpand`.

## No ambient authority at expansion time

Importing a module during expansion is allowed. Everything else that touches the outside
world is not. The host resolves imports to compiled images and hands them to the expansion
environment, so it never reads files itself. The environment then provides only pure
operations (arithmetic, strings, collections, symbols, trees, `macroexpand`). There is **no
file, network, process, environment, clock or random access**, and no I/O modules. An
imported module whose initialisation needs a missing capability fails to load, and the
error names the module and the capability ("`std::io` is not available at expansion time"),
naming the decorator or import that pulled it in.

Why: expansion runs arbitrary code chosen by the source, so its authority is deliberately
small, and a small authority makes it repeatable, which the determinism rule needs.

## Errors

An error during expansion is a **compile error**, not a runtime fault. It names the
decorator, and the tree it was applied to where that helps. Errors include: no decorator
of that name found in the imports, the decorator failed, a non-tree answer, a missing
capability, and an attempt to nest expansion.

## `@template`

`@template` (see the spec paragraph in *Decorators*) is predefined, shadowable, and is not
special-cased by spelling. Its default implementation wraps the operand as a `'template`
node, and the compiler acts on that node. A user decorator named `template` that returns
a `'template` node behaves the same.

The compiler may rewrite a lone `@template` (the only decorator on a statement, or the
innermost of several) directly, without starting an expansion environment, since that
cannot change what the program means unless a module supplies its own `template`
decorator. Such a module must not rely on shadowing for a lone `@template`; whether to
forbid this or to consult the scope first is left open.

## Reserved (designed for, not defined)

- An explicit compile-time marker on a definition (Lisp's `eval-when`), compiled into the
  expansion environment so its code can run there. This would be the only way code from the
  module being compiled could execute during expansion, and it would be visible in the
  source.
- Resource limits (memory, step count) on the expansion environment.
- Cross-module `name::$ast`, and block or expression forms of `@template`.
