# Embedding wyrm

`libwyrmhost` is wyrm as a library: a few function calls give you an interpreter you can
run code in, share variables with, load scripts into, or hand to a user as a REPL. Its
whole public surface is one header, `<wyrm/host.h>`; working programs are in
[`examples/`](../examples).

    #include <wyrm/host.h>

    wy_host* host;
    wy_host_new(NULL, &host);                         /* NULL = defaults */

    wy_host_set_int(host, "width", 40);               /* set a variable */
    wy_host_eval(host, "area := width * 2", NULL);    /* run code */
    long area;
    wy_host_get_int(host, "area", &area);             /* read it back: 80 */

    wy_host_load_file(host, "config.wy");             /* run a script */
    wy_host_repl(host, stdin, 1);                     /* or give the user a REPL */

    wy_host_free(host);

## Building against it

With meson, use the dependency your build already has (`wyrm_host_dep` inside this project;
as a subproject or an installed package, `dependency('wyrmhost')`). With pkg-config:

    cc app.c $(pkg-config --cflags --libs wyrmhost) -o app

The installed library finds its sibling `libcwyrm` through an `$ORIGIN` rpath, so no
`ldconfig` is needed. It requires a hosted C11 platform (files, POSIX descriptors).
The header is `extern "C"`-safe for C++.

## One host, one namespace

A `wy_host` owns a whole interpreter (machine, context, standard library, the embedded
compiler, import roots) and one persistent **session**. Everything you evaluate, load or set
lands in the same module-level namespace, so a variable you set from C is visible to
scripts, a script's functions can be called from C, and definitions persist from call to
call. The rules for redeclaring names (each call is a scope: redeclaring shadows, assignment
rebinds) are those of the REPL: see [repl.md](repl.md). Separate hosts are fully independent;
a host is not thread safe, so use one from one thread at a time.

## Running code

| Call | What it does |
|---|---|
| `wy_host_eval(host, source, &result)` | compile and run `source` (a whole program: statements, blocks, definitions). If its last statement is a bare expression, that is `*result` (pass NULL to ignore) |
| `wy_host_load_file(host, path)` | run a script file the same way; its directory becomes an import root so it can `import` sibling modules |
| `wy_host_error(host)` | the message for the last failure |

Results and errors:

| Code | Meaning |
|---|---|
| `WY_ERR_NONE` | it ran |
| `WY_ERR_IMAGE` | it did not compile (a syntax or semantic error) |
| `WY_ERR_FAULT` | it ran and faulted (an uncaught error at run time) |
| `WY_ERR_SESSION_FULL` | the session's reserved space is exhausted (see limits) |

After any failure the host is as usable as before. A compile error changes nothing at all; a
run-time fault keeps whatever the code had already stored.

A `wy_value` you get from `eval` is valid until the next call on that host. To keep it, store
it in a variable (`wy_host_set`) or copy it out (`wy_host_format`, or a typed getter after
storing it under a name).

## Variables

Names are plain identifiers (`[A-Za-z_][A-Za-z0-9_]*`); anything else is `WY_ERR_INVAL`, so no
source text can be smuggled in through a name.

    wy_host_set_int / _float / _bool / _string / _nil (host, name, value)
    wy_host_get_int / _float / _bool / _string (host, name, &out ...)
    wy_host_set / wy_host_get  (raw wy_value)       wy_host_has(host, name)

Setting a name that does not exist declares it; setting one that does rebinds it, and code
that already refers to it (a function reading the variable) sees the new value. Getters return
`WY_ERR_UNBOUND` for a missing name and `WY_ERR_BAD_TYPE` for a value of another type
(`get_float` also accepts an integer). `wy_host_get_string` copies into your buffer and
reports the full length, `WY_ERR_RANGE` if it was truncated.

`wy_host_format(host, value, buf, cap, &len)` renders any value as text the way `println`
does (a string is its own text): the way to turn an `eval` result into a C string.

## REPL

`wy_host_repl(host, in, interactive)` is the whole loop of `wyrm -i` over any `FILE*`
(commands, blocks, error reporting, `:reset`); prompts print only when `interactive`. To
build your own, use `wy_host_needs_more(host, text)` (true while an input is unfinished),
`wy_host_eval`, `wy_host_format` and `wy_host_error`: `examples/embed_custom_repl.c` is a
complete loop in about fifty lines.

## Configuration

`wy_host_new(config, &host)` takes a `wy_host_config` (zero it, or `wy_host_config_default`):

| Field | Meaning |
|---|---|
| `include_paths`, `include_count` | directories searched for `import`ed `.wy` sources (`wyrm -I`) |
| `cache_dir` | where compiled imports are cached; NULL = a `__wycache__` beside each source |
| `output`, `output_ud` | where `print`/`println` write (a `wy_io_write_fn`); NULL = standard output |
| `code_words` | code reservation of the session in 32-bit words; 0 = 16 MiB |
| `verbose` | one stderr line per import resolution |

The standard library (`std::io`, `std::pairs`, ...) and the compiler are compiled into
`libwyrmhost`; nothing needs to be installed next to your program.

## Limits

The session reserves about 27 MiB of address space up front (16 MiB of it code); only the
pages actually used cost memory on a hosted system. Running out returns
`WY_ERR_SESSION_FULL` for the input that did not fit and leaves the host usable. Redefinition
never reclaims old code (something may still be running it), so a long-lived host that
redefines heavily should be restarted, or use a fresh host.

## Beyond the facade

`wy_host_context(host)` returns the underlying `wy_context*`, and the full C API
(`wyrm/*.h`: values, modules, natives, coroutines) remains available for anything the
facade does not cover. Values from it live on the host's heap.

## Not covered yet

Registering C functions callable from wyrm code, calling a wyrm function with C arguments
directly (evaluate a call expression instead), and typed getters for lists and dictionaries.
See `active_issues.md`.
