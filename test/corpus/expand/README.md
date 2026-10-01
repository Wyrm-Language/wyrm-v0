Epic 10a fixtures for the C VM's expansion natives (`tree_box`, `sexpr`,
`bind_message`, and `std::expand`). They use builtins pypoc does not define,
so pypoc cannot run them and there is no pypoc-run `.out`: each `.out` is
authored by hand.

Sources only are committed. `scripts/build_fixtures.py` (a meson custom
target, run on every build) compiles every `.wy` here with the build tree's
own compiler into `<builddir>/test_fixtures/expand/*.wyd`, and
`test_bytecode_golden.cpp` runs them, seeding `__ARGS[0]` with that directory
so `expandmain` can read its sibling images (`scope.wyd`, `scope_io.wyd`).
The decorator library fixture that both engines run end to end is
`decorators/declib.wy` with `decorators/decorated.wy` (plan C collapsed the
port-shaped `wydeclib`/`wydecorated` twins into it).
