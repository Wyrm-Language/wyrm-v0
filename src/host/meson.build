# libwyrmhost: everything an embedder needs on top of libcwyrm to run wyrm source -
# the embedded compiler and library modules (../embed), the std::io natives, the
# scratch-machine compile hook, the session driver and the wy_host_* facade
# (include/wyrm/host.h). The `wyrm` executable, the C++ tests and the examples all
# link it. Hosted only: it needs files, POSIX descriptors and a C library.
host_sources = files('host.c', 'repl.c')

wyrm_host_lib = library('wyrmhost',
    host_sources + embed_sources,
    include_directories: [cwyrm_lib_inc, embed_inc, include_directories('.')],
    dependencies: [cwyrm_lib_dep],
    install: true,
    # Find libcwyrm next to it once installed (a RUNPATH on an executable is not
    # used for its indirect dependencies), so an embedder needs no ldconfig.
    install_rpath: '$ORIGIN',
)

wyrm_host_dep = declare_dependency(
    link_with: wyrm_host_lib,
    include_directories: [include_directories('../../include'), embed_inc],
    dependencies: [cwyrm_lib_dep],
)

pkgconfig.generate(wyrm_host_lib,
    name: 'wyrmhost',
    description: 'Wyrm embedding library: run wyrm source, set and read variables, load scripts, REPL',
    version: meson.project_version(),
    requires: [cwyrm_lib],
)
