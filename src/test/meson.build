test_cwyrm_sources = files(
    'main.cpp',
    'test_atomics.cpp',
    'test_context.cpp',
    'test_machine.cpp',
    'test_stack.cpp',
    'test_wyrm.cpp',
)

extra_test_inc += include_directories('.')

test_cwyrm_exe = executable('test_cwyrm',
    test_cwyrm_sources + extra_test_sources,
    include_directories: [cwyrm_lib_inc] + extra_test_inc,
    dependencies: [doctest_dep] + extra_test_deps,
    link_with: cwyrm_lib,
)

test('cwyrm', test_cwyrm_exe)
