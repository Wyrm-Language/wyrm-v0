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
    'test_heap_objects.cpp',
    'test_image.cpp',
    'test_link.cpp',
    'test_machine.cpp',
    'test_module.cpp',
    'test_host.cpp',
    'test_session.cpp',
    'test_session_compile.cpp',
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

if wy_opt.disabled()
    error('tests option requires the wyrm executable (fixtures are built by its compiler)')
endif

# Fixtures are never committed: this build tree's own compiler produces them
# from test/corpus (scripts/build_fixtures.py), so they always match the
# bytecode format under test. Rebuilt on every build (well under a second).
python = find_program('python3')
test_fixture_dir = meson.project_build_root() / 'test_fixtures'
test_fixtures = custom_target('test_fixtures',
    output: 'test_fixtures.stamp',
    command: [python, files('../../scripts/build_fixtures.py'), wyrm_exe, test_fixture_dir],
    depends: [wyrm_exe],
    depend_files: files('../../test/corpus/manifest.txt'),
    build_always_stale: true,
    build_by_default: true,
)

# The embedded-image fixture (epic 1 M4/M5): compiled straight into
# test_cwyrm so a test can load it with no file I/O at all. Generated (C
# source emitted by the compiler) rather than committed. The builtin module
# table (epic 11 M1) rides along for the import-resolution tests.
hello_1_c = custom_target('hello_1_c',
    input: files('../../test/corpus/hello_1.wy'),
    output: 'hello_1.c',
    command: [wyrm_exe, '--build-bc', '-o', '@OUTDIR@',
              '--emit', 'c', '@INPUT@'],
)

test_cwyrm_embedded_sources = [hello_1_c]

test_cwyrm_exe = executable('test_cwyrm',
    test_cwyrm_sources + extra_test_sources + test_cwyrm_embedded_sources,
    include_directories: [cwyrm_lib_inc, embed_inc] + extra_test_inc,
    dependencies: [doctest_dep, wyrmxx_lib_dep, wyrm_host_dep] + extra_test_deps,
    link_with: [cwyrm_lib, wyrmxx_lib],
    cpp_args: [
        '-DWY_TEST_DEST_INCLUDE_DIR="' + project_root / 'include' / 'wyrm' + '"',
        '-DWY_TEST_FIXTURE_DIR="' + test_fixture_dir + '"',
        '-DWY_TEST_CORPUS_DIR="' + project_root / 'test' / 'corpus' + '"',
        '-DWY_TEST_REPO_ROOT="' + project_root + '"',
    ],
)

# The REPL differential (every corpus source, one statement per input, plus a
# GC-stress subset) runs the whole embedded compiler for hundreds of inputs, so
# it is its own test with a long timeout and stays out of the default run.
test('cwyrm', test_cwyrm_exe, args: ['--test-suite-exclude=session-differential'], timeout: 240, depends: [test_fixtures])
test('cwyrm-session-differential', test_cwyrm_exe, args: ['--test-suite=session-differential'],
    suite: 'session', timeout: 1800, depends: [test_fixtures])
test('cwyrm-loader', test_cwyrm_exe, args: ['--test-suite=loader'], suite: 'loader', depends: [test_fixtures])
test('cwyrm-golden', test_cwyrm_exe, args: ['--test-suite=golden'], suite: 'golden', depends: [test_fixtures])
test('cwyrm-golden-gcstress', test_cwyrm_exe, args: ['--test-suite=golden-gcstress'], suite: 'golden', depends: [test_fixtures])
