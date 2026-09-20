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
