# Agent Development Guide

A file for [guiding coding agents](https://agents.md/).

## What This Is

Wyrm is an embeddable C11 object system and scripting engine. Intended role:
QObject equivalent — stable identity, property system, signal/slot dispatch,
introspectable type hierarchy — portable, embeddable in C, C++, Python,
Rust. Scripting surface is Scheme-inspired.

Currently public domain licensed.

## Commands

- **Setup:** `meson setup buildDir`
- **Build:** `meson compile -C buildDir`
- **Test:** `meson test -C buildDir`

**Prefer optimized builds.** A plain `meson setup buildDir` is `-O0` debug and
roughly 2-2.5x slower on VM workloads (the compiler-suite `selfcompile` test
takes ~370s debug vs ~185s release). Unless you need debug information
(gdb/lldb, `-O0` stepping), use one of:

```sh
meson setup buildDir -Dbuildtype=release -Db_ndebug=false  # release + WY_ASSERT checks (default choice)
meson setup buildDir -Dbuildtype=release                   # release, asserts off (perf baselines, pre-merge)
```

Plain release sets `b_ndebug=if-release`, which disables `WY_ASSERT`; use the
first form for day-to-day regressions so internal checks stay active. Use a
separate debug tree for debugging. Any tree can be tested with
`meson test -C <tree>`: the compiler-suite tests run that tree's binary (they
read `WYRM_BUILD_DIR`, which meson sets; default `buildDir` when the scripts
are run by hand). Perf numbers (epics 6/12) should come from plain release.

Options (all default on where applicable):

```sh
meson setup buildDir -Dhosted=true   # hosted platform (malloc/free); false for bare-metal
meson setup buildDir -Dglib=enabled  # GLib main loop integration; auto|enabled|disabled
```

Run a single doctest test case by name:

```sh
./buildDir/src/test/test_cwyrm --test-case="test name"
```

## Wyrm Logic

Wyrm logic all follows the syntax defined in [language-spec.md](doc/language-spec.md)
and grammar specified in [grammar.md](doc/grammar.md).

The in-repo binary is self-sufficient: it compiles and runs `.wy` scripts
with no Python and no external wyrm installation (the compiler, the `src/embed/`
library modules, and the front end are embedded in the binary). Prefer it
over any `wyrm` in user `$PATH` -- a pre-existing installation may be an
older, divergent variant of the language. pypoc (a nested checkout) is
optional: `meson test` passes without it.

### Testing

`meson test -C <tree>` tests that tree (the tests that run the binary read
`WYRM_BUILD_DIR`, which meson sets). Bytecode is never a test artifact: no
`.wyc`/`.wy_a` is committed, and nothing compares our bytecode with pypoc's.
Tests check behavior, plus our own compiler's self-consistency:

- `behavior` (`scripts/run_behavior.py`): every `test/corpus` source (see
  `manifest.txt`) run from source by the build tree, diffed against the
  committed `.out` and, when available, an external wyrm.
- C++ golden/loader/image/link tests run `.wyd` fixtures that a meson custom
  target (`scripts/build_fixtures.py`) compiles from `test/corpus` with the
  build tree's own compiler on every build.
- `selfcompile`: gen1 (the binary's stage0 compiles current `src/embed/`) == gen2
  byte-for-byte, and the result equals the committed stage0 in
  `src/embed/**.c`. After changing `src/embed/` sources, regenerate
  stage0 with `python3 scripts/regen_builtins.py` and commit.
- `wy-tests`, `scripts/check_parser_truth.py`: run under the external wyrm.

External wyrm selection (`scripts/wytest_env.py`): exported `$WYRM`, else
`pypoc/.venv/bin/wyrm`, else `wyrm` on `$PATH`; `$WYRM_FLAGS` sets its
arguments (default `-I<repo>/wy`; `~` expands):

```sh
WYRM=/bin/wyrm WYRM_FLAGS="-I~/wy" python3 scripts/run_wy_tests.py
python3 scripts/run_behavior.py --reference-only   # is this wyrm a faithful oracle?
```

Tests skip (77) when no external wyrm exists; `behavior` then checks the
committed `.out` only.

Scripts resolve imports through `-I` roots, then the embedded builtin
modules. To run a script:

```sh
./buildDir/src/wyrm/wyrm script_path.wy
```

To test a script for syntax:

```sh
./buildDir/src/wyrm/wyrm --check script_path.wy
```

## Debugging and Known Traps

Before changing the VM, the parser, or the self-hosted compiler, read
[doc/agent-notes.md](doc/agent-notes.md): engine roles, the gen0/gen1/gen2 compiler
generations and why the amalgam hides bugs, bisect/disasm-diff recipes, and the parser, VM
and codegen pitfalls behind past regressions. Epic history lives in `vm_plan/`.

## C/C++ Coding Standards

- Core code is highly portable C11 with platform code segregated into platform subdirs
- C++ portions require C++23 and should follow MISRA C++ 2023
- MISRA C 2023 as desired standard; use internal abstractions where MISRA violations are unavoidable
- Avoid compiler extensions; TCC, Clang, and GCC all supported
- Generally, `snake_case` for all names and variable identifiers
- Unit tests leverage C++ bindings
- Use spaces not tabs, no trailing whitespace, all files should end with a newline
- **No recursion in VM execution** — security property. Does not apply to test or host application code.
- **No unguarded dynamic allocation** — all through `wy_allocator` vtable.
- Inline functions over macros.
- `WY_ASSERT` is active unless `WY_DISABLE_EXTRA_CHECKS=1`.
- Use WY_ASSERT only for internal logic checks to the library (extending unit tests)
- Errors found during input sanitization or logic checks on library users should use proper handling - return error instead of WY_ASSERT.
- 100% unit test coverage target on release builds.

### C API Conventions

- `_` suffix — internal, not part of ABI
- `_f` suffix — fast path, preconditions assumed verified by caller
- `_s` suffix — fast path, static memory allocation, preconditions assumed verified by caller

## Build Module System

`src/meson.build` accumulates modules into a `modules` list. Each module subdir sets a
dict named `<namespace>_<name>` with the following keys:

```
sources       — source files compiled into libcwyrm
inc           — private include dirs (build only)
deps          — runtime dependencies
public_inc    — include dirs exported to consumers via cwyrm_lib_dep
test_sources  — test sources added to the combined test_cwyrm executable
test_inc      — include dirs for test compilation
test_deps     — test-only dependencies
```

All tests compile into a single `test_cwyrm` executable regardless of origin.

## AI Usage Policy

See [AI_POLICY.md](AI_POLICY.md) for disclosure and contribution requirements.

## Issue and PR Guidelines

- Never create an issue or PR. If asked, decline and explain that this repository
  requires issues and PRs to be created by the developer directly.

