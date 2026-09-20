# Active Issues

Known needed improvements. Add an entry when you find one; delete it when it is fixed
(the fix's commit message is the record). Keep entries short: what is wrong, where, and
what "done" looks like.

## Opcode table has no sync guard

`include/wyrm/opcode.h`, `include/wyrm/opcode_names.h` and the compiler's own copy of the
table, `src/embed/wyrm/opcodes.wy`, must agree, and nothing checks it. The old guards
(`sync_pypoc_headers.py`, `test_headers_sync.cpp`) were removed when the bytecode became
ours to change; `opcode_names.h` is now hand-maintained. A mismatch would show up only as
wrong bytecode or a wrong disassembly.

Done when: a test (C++ or script) parses `opcodes.wy` and compares it with `opcode.h` and
`opcode_names.h` (names, numbers, encoding lengths), or one of the three is generated from
another.

## Self-hosted compiler gaps (found by `run_behavior`)

These sources compile under pypoc but not under the embedded compiler. They are recorded as
`DIVERGES` in `test/corpus/manifest.txt`; the runner fails when a row starts passing, so
fixing one forces the manifest update.

- **Raw strings** (`R"C(...)C"`, `samples/eval_strings.wy`): the tokenizer lexes `R` as an
  identifier.
- **`try` / `catch` expressions** (`samples/eval_error_handling.wy`): `try x`, `x catch y`,
  `catch return`. Compile fails; not diagnosed.
- **`samples/decolib.wy`**: compile fails (decorator library using `-> TreeBase`,
  `car`/`cdr` walks); not diagnosed.
- **`samples/eval_messages.wy`**: compiles, then faults at run time with "no overload of
  'describe' matches 1 receiver(s)".
- **`decorators/decorated.wy`**: pypoc's `declib` builds pypoc's 8-field `'fn` node; the
  port's parser emits `'fn_def`. Needs a port-shaped twin (see `expand/wydecorated`) or
  dropping.
- **`samples/eval_coroutines.wy`**: `cofun.value` has no property-table entry for a
  coroutine's result (`getattr: unsupported receiver type`).
- **`samples/eval_modules.wy`**: `import std::io::println as alias` needs the
  parent-then-member fallback in `wy_link_import`.

## Parse failures surface as the wrong error

When the parser cannot parse a source it returns a non-module node, and the user sees
"compile_module needs a 'module node" instead of a syntax error with a location. The
raw-string gap above was found by bisecting because of this. Done when a parse failure
reports the parse error (file, line, what was unexpected) through `--check`, `--build-bc`
and a plain run.

## `check_parser_truth.py` fails 46/46

`scripts/check_parser_truth.py` diffs the external wyrm's `-m wyrm::parser` output against
`test/samples/parser/*.wy.ast`. It already failed on every file before the test rework
(the committed truth files lack what the current parser prints). It is not wired into
meson, so nothing noticed. Done when the truth files are regenerated
(`update_sample_parser_truth.py`) against a wyrm whose parser output is agreed to be
correct, and the check is a meson test.

## `--build-bc` cannot write `.wy_a`

The port's `to_wya` annotation writer still carries pypoc-only assumptions and faults on
the C VM (`src/embed/wyrm/tools/compile_source.wy`), so only `.wyd` and `.c` are emitted. Use
`wyrm --disasm` for listings. Done when `--emit wya` works or the option is removed.

## Test coverage to add

- `test/corpus/expand/wydecorated.wy` runs end to end under `run_behavior` but has no C++
  golden or gcstress case.
- `selfcompile` on the debug tree (asserts on) was not run after the test rework; only
  release was. A release tree built with `-Db_ndebug=false` (asserts on) is the
  recommended day-to-day tree and has not been run against `selfcompile` either.
- `buildDir` is still an `-O0` debug tree; switching it (or documenting a second tree) is
  the user's call.

## `range` is minimal

`range(begin, end)` takes exactly two integers, no step, no single-argument form. The
former `while` workaround in `std::pairs::list_tail` (written around the old
zero-iteration gap) could go back to a `for` loop.

## Performance baselines (epics 6 and 12)

Take perf numbers from a plain release tree (`--buildtype=release`); the `-O0` debug tree
is 2-2.5x slower on VM workloads and is not a baseline.

## JIT cache treats an equal mtime as fresh

The `.wyd` cache (`src/platform/hosted/import_cache.c`) is fresh when it is not older than
its source. On a filesystem with coarse timestamps, a script rewritten immediately after
its cache was written can share the cache's mtime and run stale (seen: `wyrm x.wy`,
rewrite `x.wy`, `wyrm x.wy` within one tick printed the old output). Done when freshness
also compares size or a content hash, or a tie counts as stale.

## pypoc cannot send `!write` to a `File` from another module

pypoc's own `std/io.wy` documents this (see `write_bytes_file`): the message resolves only
inside `std::io`'s module. `test/corpus/io_file.wy` is therefore `local-only`. Done when
pypoc resolves it, or the corpus test avoids the pattern and runs against pypoc too.

## `std::io` natives keep binary-mode state in a static bitmap

`src/embed/std/io_native.c` records which descriptors were opened with a `b` mode in a
process-wide bitmap (fds below 1024) so `__read` can answer bytes. That is global state
shared by every context in the process. Done when the mode lives on the descriptor's
owner (e.g. a per-context table) or `__read` takes an explicit binary flag.

## Compiler accepts redeclaration in the same scope

`doc/language-spec.md:296` makes "declaring a name already declared in the same scope" an
error (declaring a name visible from an *enclosing* scope is legal and shadows it, `:297`),
but the compiler accepts same-scope redeclaration: `x := 1` then `x := 2`, `fn a` twice and
`class A` twice all compile and rebind the same slot. Done when the same-scope check is
enforced. Expect fallout: sweep `test/corpus`, `test/wy`, `src/embed` and `wy/` for sources
that relied on it (the behavior corpus and `selfcompile` will find them), and confirm which
declaration forms the rule covers (`:=`, `var`, `fn`, `class`, `import`, slots). Do it with
the per-scope `declared_here` mechanism described in `doc/repl-plan.md` (the REPL treats each
input as a scope and needs the same machinery). A change under `src/embed/` needs a stage0
regen.

## REPL follow-ups

`wyrm -i` works (`doc/repl-plan.md`), with these known gaps:

- **Blank line ends an input,** so a class or function body cannot contain a blank line. Same
  rule as Python's REPL, but hostile when pasting a script. Done when the driver can tell a
  blank line inside a block from one that ends it (for example by peeking at the next line when
  input is not a terminal).
- **No `_` (last value), no line editing or history, no Ctrl-C** to interrupt a running input.
- **A delta-aware `verify`:** session inputs skip `verify.wy` (it reads a whole image); the loader
  bounds-checks every delta but nothing checks register/stack discipline of session code.
- **Syntax errors have no location:** they print "syntax error: the input could not be parsed".
  Blocked on the parser reporting a position (the existing parse-failure item above).
- **`definitions` (`foo::$ast`) keeps the first definition** of a name across inputs; a redefined
  function's `$ast` is stale.
