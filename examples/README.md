# Examples: embedding wyrm

Each is a small C program using `<wyrm/host.h>` (libwyrmhost); see
[doc/embedding.md](../doc/embedding.md) for the full guide. They are built with the
project and checked by the `examples` meson test.

| File | Shows |
|---|---|
| `embed_basic.c` | create a host, set variables, `eval` code, read variables back, format a result, handle compile errors and faults |
| `embed_script.c` | pass variables to a script, `wy_host_load_file` it (with an imported sibling module in `scripts/`), read what it produced and call its functions |
| `embed_repl.c` | give users a REPL in a dozen lines: `wy_host_repl` |
| `embed_custom_repl.c` | build your own loop from `wy_host_needs_more` / `wy_host_eval` / `wy_host_format` |

`scripts/` holds the wyrm sources the examples load; `expected/` the outputs (and REPL
input transcripts) the test compares against.

    meson compile -C buildDir
    ./buildDir/examples/embed_basic
    ./buildDir/examples/embed_script
    printf 'greeting\nvolume * 2\n' | ./buildDir/examples/embed_repl

Building one against an installed wyrm:

    cc embed_basic.c $(pkg-config --cflags --libs wyrmhost) -o embed_basic
