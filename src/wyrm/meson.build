wyrm_exe = executable('wyrm',
    files('main.c'),
    dependencies: [cwyrm_lib_dep],
    install: true,
)

# scripts/check_disasm.sh: diffs `wyrm --disasm` against every fixture's
# checked-in .wy_a listing (epic 1 M5). Needs the corpus (test/bytecode/),
# not just this binary, so it is skipped rather than failed when that's
# absent from a partial checkout.
check_disasm = find_program('../../scripts/check_disasm.sh', required: false)
if check_disasm.found()
    test('disasm-check', check_disasm, args: [wyrm_exe.full_path()], suite: 'disasm')
endif
