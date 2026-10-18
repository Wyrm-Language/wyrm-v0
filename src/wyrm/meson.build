# The executable is a thin front end: the embedded compiler and library images
# (../embed), the std::io natives, the scratch compile hook and the REPL all live
# in libwyrmhost (../host), which every embedder links too.
wyrm_exe = executable('wyrm',
    files('main.c'),
    dependencies: [wyrm_host_dep],
    install: true,
)

# Epic 11 M2: `wyrm script.wy` compiles in-process through the embedded
# compiler; its output must match the same script run from a .wyd built with
# --build-bc, and (epic 11 M6) with no Python reachable on PATH.
check_wy_entry = find_program('../../scripts/check_wy_entry.sh', required: false)
if check_wy_entry.found()
    test('wy-entry', check_wy_entry, args: [wyrm_exe.full_path(), meson.project_source_root()], suite: 'wyrm')
endif

# Epic 11 M3: the .wyd cache lifecycle - jit on a miss, cache hits, touch
# invalidation, --cache-dir prefix mapping, .wyc cache files ignored, and a cache
# from another format version recompiled.
check_wy_cache = find_program('../../scripts/check_wy_cache.sh', required: false)
if check_wy_cache.found()
    test('wy-cache', check_wy_cache, args: [wyrm_exe.full_path(), meson.project_source_root()], suite: 'wyrm')
endif

# Epic 11 M4: --check, --build-bc (containers byte-identical to the cache's),
# -m, and the exit-code contract.
check_wy_flags = find_program('../../scripts/check_wy_flags.sh', required: false)
if check_wy_flags.found()
    test('wy-flags', check_wy_flags, args: [wyrm_exe.full_path(), meson.project_source_root()], suite: 'wyrm')
endif

# The `trap([code])` intrinsic (repl-plan M-1): faults per code, shadowable,
# literal 0-255 only.
check_wy_trap = find_program('../../scripts/check_wy_trap.sh', required: false)
if check_wy_trap.found()
    test('wy-trap', check_wy_trap, args: [wyrm_exe.full_path()], suite: 'wyrm')
endif

# The interactive loop (`wyrm -i`, repl-plan M3): piped transcripts under
# test/corpus/repl, local only.
check_wy_repl = find_program('../../scripts/check_wy_repl.sh', required: false)
if check_wy_repl.found()
    test('wy-repl', check_wy_repl, args: [wyrm_exe.full_path(), meson.project_source_root()], suite: 'wyrm', timeout: 300)
endif

# The wire-encoding encoder (src/embed/wyrm/cbor.wy) against the Worked
# examples of project design/wire_encoding.md, byte for byte.
check_cbor = find_program('../../scripts/check_cbor.sh', required: false)
if check_cbor.found()
    test('wy-cbor', check_cbor, args: [wyrm_exe.full_path(), meson.project_source_root()], suite: 'wyrm')
endif
