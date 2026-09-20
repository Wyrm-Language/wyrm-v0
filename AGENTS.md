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
with no Python and no external wyrm installation (the compiler, the `wy/`
library modules, and the front end are embedded in the binary). Prefer it
over any `wyrm` in user `$PATH` -- a pre-existing installation may be an
older, divergent variant of the language. pypoc (a nested checkout) is
optional: it regenerates golden fixtures and the compiler-suite seeds, and
`meson test` passes without it.

Scripts resolve imports through `-I` roots, then the embedded builtin
modules. To run a script:

```sh
./buildDir/src/wyrm/wyrm -Iwy script_path.wy
```

To test a script for syntax:

```sh
./buildDir/src/wyrm/wyrm -Iwy --check script_path.wy
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

