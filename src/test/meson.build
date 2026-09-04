test_cwyrm_sources = files(
    'main.cpp',
    'test_allocator.cpp',
    'test_atomics.cpp',
    'test_bson.cpp',
    'test_class.cpp',
    'test_context.cpp',
    'test_fiber.cpp',
    'test_machine.cpp',
    'test_slot.cpp',
    'test_stack.cpp',
    'test_state.cpp',
    'test_wbox.cpp',
    'test_wgc.cpp',
    'test_wpair.cpp',
    'test_wstring.cpp',
    'test_wtable.cpp',
    'test_wvm.cpp',
    'test_wyrm.cpp',
)

extra_test_inc += include_directories('.')

test_cwyrm_exe = executable('test_cwyrm',
    test_cwyrm_sources + extra_test_sources,
    include_directories: [cwyrm_lib_inc] + extra_test_inc,
    dependencies: [doctest_dep, wyrmxx_lib_dep] + extra_test_deps,
    link_with: [cwyrm_lib, wyrmxx_lib],
)

test('cwyrm', test_cwyrm_exe)
