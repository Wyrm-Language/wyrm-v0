Epic 10a fixtures for the C VM's expansion natives (`tree_box`, `sexpr`,
`bind_message`, and later `std::expand`). They use builtins pypoc does not
define, so pypoc cannot compile them and there is no pypoc-run `.out`: each
`.out` is authored by hand and checked by `test_bytecode_golden.cpp` only.

Regenerate `.wyc` with the self-hosted compiler (needs a gen1 tree from
`SELFCOMPILE_KEEP=<dir> python3 scripts/run_selfcompile.py`):

    T=<dir>/gen1
    ./buildDir/src/wyrm/wyrm -I$T -I$T/wyrm -I$T/wyrm/compiler \
        $T/wyrm/tools/compiler_main.wyc --mirror test/bytecode/expand \
        <out-dir> treelib.wy treemain.wy
