project('wyrm', 'c', 'cpp',
    version: '0.2.0',
    meson_version : '>=1.1.0',
    default_options: [
        'cpp_std=c++23',
        'c_std=c11',
        'warning_level=3',
    ],
)

subdir('src')

if get_option('hosted') and not get_option('wyrm').disabled()
    subdir('examples')
endif

# Epic 8 M5: run every test/wy/*.wy under an external wyrm ($WYRM/$WYRM_FLAGS,
# else pypoc's venv wyrm, else `wyrm` on PATH; see scripts/wytest_env.py) -
# the self-hosted front-end tests (parser/tokenizer/DSL macro suite written in
# wyrm itself, not this project's C VM). Skipped when none is available.
run_wy_tests = find_program('scripts/run_wy_tests.py', required: false)
if run_wy_tests.found()
    test('wy-tests', run_wy_tests, suite: 'wy')
endif

# Behavioral conformance: every test/corpus source, run from source by this
# build tree and diffed against its committed .out and (when one is
# available - $WYRM/$WYRM_FLAGS, else pypoc, else PATH) an external wyrm.
run_behavior = find_program('scripts/run_behavior.py', required: false)
if run_behavior.found()
    test('behavior', run_behavior, suite: 'behavior', timeout: 600, workdir: meson.project_source_root(),
         env: {'WYRM_BUILD_DIR': meson.project_build_root()})
endif

# The self-hosted compiler on the C VM: the binary's embedded stage0 compiles
# the current wy/ sources (gen1), gen1 compiles them again (gen2), and the two
# trees must be byte-identical and match the committed stage0
# (src/wyrm/embedded/). No pypoc; see the script header.
run_selfcompile = find_program('scripts/run_selfcompile.py', required: false)
if run_selfcompile.found()
    test('selfcompile', run_selfcompile, suite: 'compiler', timeout: 1800, workdir: meson.project_source_root(),
         env: {'WYRM_BUILD_DIR': meson.project_build_root()})
endif
