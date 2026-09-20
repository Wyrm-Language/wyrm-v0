test_cwyrm_sources = files(
    'main.cpp',
    'test_allocator.cpp',
    'test_atomics.cpp',
    'test_bson.cpp',
    'test_builtins.cpp',
    'test_bytecode_golden.cpp',
    'test_class.cpp',
    'test_context.cpp',
    'test_coroutine.cpp',
    'test_dispatch.cpp',
    'test_fiber.cpp',
    'test_headers_sync.cpp',
    'test_heap_objects.cpp',
    'test_image.cpp',
    'test_link.cpp',
    'test_machine.cpp',
    'test_module.cpp',
    'test_slot.cpp',
    'test_stack.cpp',
    'test_symtab.cpp',
    'test_vm_call.cpp',
    'test_wbox.cpp',
    'test_wgc.cpp',
    'test_wpair.cpp',
    'test_wstring.cpp',
    'test_wtable.cpp',
    'test_wvm.cpp'
)

extra_test_inc += include_directories('.')

project_root = meson.project_source_root()

# The embedded-image fixtures (epic 1 M4/M5): compiled straight into
# test_cwyrm so a test can load them with no file I/O at all. The builtin
# module table (epic 11 M1) rides along for the import-resolution tests.
test_cwyrm_embedded_sources = files(
    '../../test/bytecode/embedded/hello_1.c',
    '../wyrm/embedded/embedded_images.c',
    '../wyrm/embedded/wyrm_builtins_table.c',
)

test_cwyrm_exe = executable('test_cwyrm',
    test_cwyrm_sources + extra_test_sources + test_cwyrm_embedded_sources,
    include_directories: [cwyrm_lib_inc] + extra_test_inc,
    dependencies: [doctest_dep, wyrmxx_lib_dep] + extra_test_deps,
    link_with: [cwyrm_lib, wyrmxx_lib],
    cpp_args: [
        '-DWY_TEST_DEST_INCLUDE_DIR="' + project_root / 'include' / 'wyrm' + '"',
        '-DWY_TEST_PYPOC_INCLUDE_DIR="' + project_root / 'pypoc' / 'wypoc' / 'compiler_bc' / 'include' / 'wyrm' + '"',
        '-DWY_TEST_HELLO_WYC_PATH="' + project_root / 'test' / 'bytecode' / 'hello.wyc' + '"',
        '-DWY_TEST_BYTECODE_DIR="' + project_root / 'test' / 'bytecode' + '"',
        '-DWY_TEST_REPO_ROOT="' + project_root + '"',
    ],
)

test('cwyrm', test_cwyrm_exe)
test('cwyrm-loader', test_cwyrm_exe, args: ['--test-suite=loader'], suite: 'loader')
test('cwyrm-golden', test_cwyrm_exe, args: ['--test-suite=golden'], suite: 'golden')
test('cwyrm-golden-gcstress', test_cwyrm_exe, args: ['--test-suite=golden-gcstress'], suite: 'golden')
