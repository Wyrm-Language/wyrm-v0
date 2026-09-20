# Epic 11 M1: the builtin module table (embedded compiler + library images)
# is tool payload, not libcwyrm payload: the generated sources are checked in
# under embedded/ and compiled into the wyrm executable (and into test_cwyrm)
# only. embedded_images.c is a unity include of every image translation unit,
# so a regen never touches this file.
wyrm_exe = executable('wyrm',
    files('main.c',
          'embedded/embedded_images.c',
          'embedded/wyrm_builtins_table.c'),
    dependencies: [cwyrm_lib_dep],
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
# invalidation, --cache-dir prefix mapping, and .wyc cache files ignored.
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
