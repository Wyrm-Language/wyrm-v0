project('wyrm', 'c', 'cpp',
    version: '0.2.0',
    meson_version : '>=1.1.0',
    default_options: [
        'cpp_std=c++23',
        'c_std=c11',
        'warning_level=3',
        'b_ndebug=if-release',
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
    test('wy-tests', run_wy_tests, suite: 'wy', timeout: 600)
endif

# Behavioral conformance: every test/corpus source, run from source by this
# build tree and diffed against its committed .out and (when one is
# available - $WYRM/$WYRM_FLAGS, else pypoc, else PATH) an external wyrm.
run_behavior = find_program('scripts/run_behavior.py', required: false)
if run_behavior.found()
    test('behavior', run_behavior, suite: 'behavior', timeout: 600, workdir: meson.project_source_root(),
         env: {'WYRM_BUILD_DIR': meson.project_build_root()})
endif

# The shared AST conformance corpus (the sibling project repo's conformance/,
# when it is checked out): this tree's parser, through
# `wyrm::tools::parse_dump --scheme`, against the canonical trees. Cases this
# repo does not implement yet carry a `wyrm-v0` line in their `.skip` file.
conformance_check = find_program(meson.project_source_root() / '../project/conformance/check.py',
                                 required: false)
if conformance_check.found()
    test('conformance', conformance_check, suite: 'conformance', timeout: 600,
         args: ['--producer', 'wyrm-v0', '--root', meson.project_source_root(),
                '--build', meson.project_build_root(), '--json'])
endif

# The parser's golden trees: test/samples/parser/*.wy against their committed
# .wy.ast (D2 Scheme form, this tree's own parser). Regenerate with
# scripts/update_sample_parser_truth.py after a deliberate parser change.
check_parser_truth = find_program('scripts/check_parser_truth.py', required: false)
if check_parser_truth.found()
    test('parser-truth', check_parser_truth, suite: 'parser', workdir: meson.project_source_root(),
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
