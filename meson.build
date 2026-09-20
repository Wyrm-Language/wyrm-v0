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

# Epic 8 M5: run every test/wy/*.wy under pypoc's own venv wyrm binary
# (self-hosted front-end tests - the parser/tokenizer/DSL macro suite
# written in wyrm itself, not this project's C VM). Skipped, not failed,
# when pypoc/.venv isn't set up (a partial checkout without the nested
# pypoc/ submodule initialized).
run_wy_tests = find_program('scripts/run_wy_tests.py', required: false)
if run_wy_tests.found()
    test('wy-tests', run_wy_tests, suite: 'wy')
endif

# Epic 10 M7: the ported compiler, run on the C VM. The corpus sweep compiles
# every corpus fixture through it and diffs the runs against .out; the
# self-compile drives the fixed point (gen1 == gen2 byte-for-byte over the
# 22 decorator-free modules, with a pypoc-built parser.wyc - see the script
# header; the true 23-module fixed point is epic 10a M5). Both skip when
# pypoc/.venv is absent.
run_corpus_sweep = find_program('scripts/run_corpus_sweep.py', required: false)
if run_corpus_sweep.found()
    test('corpus-sweep', run_corpus_sweep, suite: 'compiler', timeout: 900, workdir: meson.project_source_root())
endif
run_selfcompile = find_program('scripts/run_selfcompile.py', required: false)
if run_selfcompile.found()
    test('selfcompile', run_selfcompile, suite: 'compiler', timeout: 1800, workdir: meson.project_source_root())
endif
