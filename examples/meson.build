# Examples of embedding wyrm with libwyrmhost (include/wyrm/host.h). They are
# ordinary programs linked against `wyrm_host_dep`; each is also a meson test whose
# output is compared with examples/expected/.
example_names = ['embed_basic', 'embed_script', 'embed_repl', 'embed_custom_repl']
example_args = {
    'embed_script': ['-DEXAMPLE_SCRIPT="' + meson.current_source_dir() / 'scripts' / 'report.wy' + '"'],
}

foreach name : example_names
    exe = executable(name, name + '.c',
        c_args: example_args.get(name, []),
        dependencies: [wyrm_host_dep],
    )
    set_variable('example_' + name, exe)
endforeach

check_examples = find_program('../scripts/check_examples.sh', required: false)
if check_examples.found()
    test('examples', check_examples,
        args: [meson.current_source_dir(), example_embed_basic, example_embed_script,
               example_embed_repl, example_embed_custom_repl],
        suite: 'examples', timeout: 300)
endif
