# The interactive REPL

`wyrm -i` reads input from standard input and runs it in one persistent session. Nothing
you enter is ever run twice: each complete input is compiled on its own and only its own
code runs, while everything defined earlier - variables, functions, classes, imports,
live coroutines - stays usable.

    $ wyrm -i
    >>> x := 40
    >>> x + 2
    42
    >>> fn twice(n):
    ...     return n * 2
    ...
    >>> twice(x)
    80

Prompts (`>>> `, `... `) appear only when standard input is a terminal; a piped transcript
prints just the output. Other options (`-I`, `--cache-dir`, `-v`) work as for a script;
`-i` cannot be combined with a script, `-m`, `--check`, `--build-bc`, `--sections` or
`--disasm`.

## Entering input

An input is complete when nothing is left open. The REPL keeps reading (prompt `... `) while

- a `(`, `[` or `{` is still open,
- a raw string or character literal has not ended, or
- a block is in progress (a line ending in `:`, or indented lines).

**A blank line always ends the input.** So a function or class body cannot contain a blank
line. (Python's REPL has the same rule.) If input ends while something is half-typed, it is
submitted so you see the error.

A bare expression as the last statement is the input's value and is printed unless it is
`nil`: strings in single quotes, everything else the way `println` shows it.

## Commands

A line starting with `:` when nothing is pending is a command.

| Command | Effect |
|---|---|
| `:quit`, `:q` (or end of input) | leave |
| `:reset` | start a fresh session; the old one is released |
| `:help` | one-line reminder |

## Scoping: each input is a scope

The language forbids declaring a name twice in the same scope but allows a nested scope to
shadow an outer one. Each input is a scope nested inside the ones before it, so:

- **Redeclaring shadows.** After `fn a` / `fn b(): return a()` / `fn a` again, `b()` still
  calls the first `a` (it was compiled against it); new inputs see the new `a`.
- **Assignment rebinds.** `a = fn(): 3` writes the binding `a` currently names, and every
  caller of that binding sees it.
- **Redeclaring in the same input** is the usual error.
- **Forward references work.** A name used before anything defines it compiles; calling it
  before it exists faults (`fault: unbound global 'name'`), and the first later
  declaration of the name fills it, so earlier code that used it starts working. Later
  declarations shadow.
- Redefining a class shadows it: existing instances keep the old class and methods.

## Errors and faults

A compile error prints `error: ...` and changes nothing: numbering, definitions and state
are exactly as before, so the next input can reuse them. A run-time fault prints
`fault: ...`; whatever the input had already stored stays stored. Neither ends the session.
A syntax error says only what the parser expected, such as "parse: statement end" (there
is no location yet).

`trap()` (or `trap(n)`) is a deliberate breakpoint: it faults with "debugger break".

## Limits

The session reserves its arrays up front (about 27 MiB of address space, 16 of it code; on
Linux only the pages actually used cost memory). The code area is 16 MiB (4 Mi 32-bit words) by
default. Running out prints `error: session limit reached; :reset to start over` and
refuses only the input that did not fit. Set `WYRM_SESSION_CODE_WORDS` to a different
number of words to change it (mostly useful for tests).

Redefinition never reclaims old code: a closure or coroutine may still be running it.
Heavy redefinition uses code space; `:reset` recovers it.

## Not supported (yet)

`_` (the last value), line editing and history, interrupting a running input with Ctrl-C,
syntax errors with a location, and decorators defined in the same session (decorators
come from `-I` roots or the embedded library, because expansion runs in an isolated VM).
See `active_issues.md`.

## As a library

Everything here is available to your own programs: `wy_host_repl` is this loop over any
`FILE*`, and `wy_host_eval` / `wy_host_needs_more` are its pieces. See
[embedding.md](embedding.md) and `examples/embed_repl.c`.

## How it works

See `doc-llm/history/repl/repl-plan.md` for the design and `doc/vm_impl.md` ("Session modules") for the
embedding API. In short: the session is one module whose arrays are reserved at full
size and only appended to, so running frames never see a pointer move; each input compiles
to a delta image against a persistent compiler-side session and is loaded with
`wy_module_extend`.
