wyrmxx_sources = files(
    'context.cpp',
    'state.cpp'
)

wyrmxx_lib = library('wyrmxx',
    wyrmxx_sources,
    include_directories: cwyrm_lib_inc,
    dependencies: cwyrm_lib_dep,
    install: true,
)

wyrmxx_lib_dep = declare_dependency(
    link_with: wyrmxx_lib,
    include_directories: include_directories('../../include'),
    dependencies: cwyrm_lib_dep,
)

install_subdir('../../include/wyrmxx', install_dir: get_option('includedir'))
