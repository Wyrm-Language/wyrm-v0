# vm_plan — C bytecode VM, `bytes` type, self-hosted compiler

Plan written 2026-09-15. Eleven epics in three phases. Each epic is a self-contained brief
for a fresh agent session; `design_c_vm.md` is the shared architecture for epics 2–6.

## Goal

1. **Phase A (epics 1–6):** a C11 VM in this repo that loads and runs `.wyc` module images
   produced by the Python proof-of-concept compiler in `pypoc/` (`wyrm --build-bc`), with a
   conformance corpus, a real `wyrm file.wyc` CLI, and a moderately performant embedding
   API. At the end of Phase A the toolchain needs *both* `pypoc/` (to compile) and this
   repo (to run).
2. **Phase B (epic 7):** the `bytes` fundamental type (mutable, resizable byte array) in
   the spec, in pypoc, and in the C VM. Prerequisite for writing binary images from wyrm.
3. **Phase C (epics 8–11):** clean up the wyrm-in-wyrm AST, port the bytecode compiler to
   wyrm (`wy/wyrm/`), including a minimal bjson writer, until `.wy` files compile to
   bytecode on the C VM without Python, and the compiler compiles itself.
4. **Epic 12 (post-Phase-C):** the message-dispatch inline cache and per-class slot dict
   originally scoped into epic 6, deliberately deferred until the dispatch/class code
   finishes moving through the compiler port.

## Decisions fixed at plan time

- Format contract: `pypoc/doc/wyc-format.md` is normative. `opcode.h` and `image.h` are
  copied verbatim from `pypoc/wypoc/compiler_bc/include/wyrm/` and drift is a test failure.
- The binary type is named `bytes` (the spec's existing name). No `buffer` type.
- `pypoc/` is a nested git checkout, gitignored. Tooling uses `pypoc/.venv/bin/wyrm`.
- Test bytecode (`.wyc`, `.wy_a`, `.out`, `manifest.txt`) is committed under
  `test/bytecode/` so `meson test` never needs Python. Note `.gitignore` currently ignores
  `*.wyc` globally; epic 1 adds the `!test/bytecode/**` exception.
- Conformance discipline copied from `pypoc/test/test_vm_samples.py`: every fixture and
  sample is in exactly one of **matches**, **REFUSED(reason)**, **DIVERGES(reason)**, and a
  category change fails a test rather than moving silently.
- AGENTS.md rules apply throughout: no C recursion in VM execution, every allocation via
  `wy_allocator`, `WY_ASSERT` only for internal invariants, tests in doctest via the C++
  bindings, 100% coverage target.

## Phase map

| Epic | Title | Unlocks | Depends on |
|---|---|---|---|
| 1 | Toolchain, conformance corpus, image loader | every `.wyc` loads; CLI prints sections | — |
| 2 | Foundations + core interpreter | hello, arith, control_flow, multiret | 1 |
| 3 | Data, closures, errors, defers | closures, collections, errors + 8 samples | 2 |
| 4 | Classes, instances, messages | classes, messages, eval_messages | 3 |
| 5 | Modules, imports, coroutines, CLI | two_module, wildcard, coroutines, decorators; full sweep | 4 |
| 6 | Host API, hardening, baseline benchmarks | corelib tokenizer runs on C VM; benchmarks | 5 |
| 7 | `bytes` type (spec, pypoc, C) | wyrm can write binary files | 6 (C side), 3 (minimum) |
| 8 | Front-end cleanup and verification | parser goldens pass on both engines | 6 |
| 9 | bjson + image writer in wyrm | `hello.wyc` written from wyrm, byte-identical | 7, 8 |
| 10 | Compiler port | corpus compiled by wyrm; self-compile fixed point | 9 |
| 11 | Bootstrap integration | `wyrm script.wy` with no Python | 10 |
| 12 | Dispatch performance: inline cache, slot dict | measured method-call speedup over epic 6's baseline | 11 |

Epic 7 can start any time after epic 3 on the C side and after epic 1 on the pypoc side;
it is placed after 6 so Phase A stays focused. Epic 8 can run in parallel with 6. Epic 12
runs last, after epic 11, once the dispatch/class code it optimises has stopped moving
(it was originally scoped into epic 6 as M3; see epic_6.md's opening note and epic_12.md).

## Epic protocol

Every epic runs as: **scan → execute → report**.

1. **Scan** (start of session, Opus). Read this README, the epic file, the previous
   `epic_(N-1)_report.md`, and `doc/EXPLAINER.md`. Run the epic's *state-scan checklist*
   using Explore subagents, not by reading whole files. Confirm or refute each item in the
   epic's *Assumptions* list. If a milestone is invalidated, re-cut it in the epic file
   *before* starting and note the change in the report. Run the existing test suite and
   record the count.
2. **Execute** (Opus or Sonnet per milestone; see staging). Milestones in order unless the
   epic marks them independent. Commit per milestone with a message that names the epic
   and milestone (`E2/M2: core loop runs hello.wyc`). `meson test` green at every commit.
   Update `doc/EXPLAINER.md` and `doc/vm_impl.md` when a structural fact changes.
3. **Report** (end of session, Opus). Write `vm_plan/epic_N_report.md` using the template
   below. The report is the only hand-off to the next epic; the next session will not see
   this session's chat.

## Model staging

| Work | Model | Why |
|---|---|---|
| Scan, milestone re-cutting, report | Opus | judgement across many files; delegates reading to Explore subagents |
| Design-heavy milestones: E2 frame model + loop, E4 dispatch and `super`, E5 coroutines + linking, E10 compiler architecture, E12 inline cache | Opus | first-of-kind decisions with no reference implementation |
| Spec-driven work: loader sections, BSON strictness, opcode handlers once the loop exists, builtins, corpus script, doctests, `bytes` methods, bjson port, docs, parser fixes | Sonnet | the contract is written down; correctness is checked by running the corpus |
| Fan-out inside a milestone: opcode groups, builtin families, one fixture per agent, one sample per agent | Sonnet subagents, 2–3 at once, disjoint files, each with its own acceptance command | independent and testable in isolation |

Each epic file marks every milestone with a suggested model and whether it may fan out.

## Token conventions for executors

- Load only the files in the epic's *Context to load*, in that order. The list is sized to
  fit comfortably with room for work; it is not a reading list to exceed.
- Use Explore subagents for "where is X" and "what does Y do" questions. Ask them for
  conclusions with `file:line` references, not file dumps.
- Never paste `.wy_a` listings, `.out` files, or BSON hex into context. `grep -n` them.
- Run `meson test -C buildDir` with `--test-case="…"` or the fixture name while iterating;
  run the full suite only before a commit.
- Prefer `pypoc/.venv/bin/wyrm --build-bc --emit wya file.wy` and reading the specific
  instruction lines you need over re-deriving lowering from the compiler source.
- Keep `doc/EXPLAINER.md` accurate; it is what makes the next session's scan cheap.
- When a subagent is spawned, give it: the acceptance command, the files it owns, the
  files it must not touch, and the section of `design_c_vm.md` it implements.

## Report template (`epic_N_report.md`)

```markdown
# Epic N report — <title>

Session dates: … · Models used: … · Commits: <first>..<last>

## Landed
- M1 … (one line each, with the acceptance command that now passes)

## Deviations from the epic file
- <what changed and why; reference design_c_vm.md sections amended>

## Tests
- before: <N cases / M assertions>; after: <N / M>; corpus: <matches / REFUSED / DIVERGES counts>

## Open questions and known gaps
- …

## Proposed edits to epic_(N+1).md
- <assumptions now known false, milestones to re-cut, files that moved>

## Orientation for the next session
- <3–8 bullets a fresh agent needs: where the loop lives, how to run one fixture, gotchas>
```

## Files in this directory

- `README.md` — this file.
- `design_c_vm.md` — C VM architecture (frames, loop, coroutines, values, linking,
  symbols, classes, GC, testing, milestone table M0–M8). Epics 2–6 cite its sections.
- `epic_1.md` … `epic_11.md` — one brief per epic, same seven sections each.
- `epic_N_report.md` — written by the executing session at the end of epic N.
