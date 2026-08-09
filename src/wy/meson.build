wy_sources = files(
    'wy_ctx_main.c'
)

wy_lib = library('wy',
                 wy_sources,
                 include_directories: cwyrm_lib_inc,
                 dependencies: cwyrm_lib_dep,
                 install: true,
)

wy_lib_dep = declare_dependency(
    link_with: wy_lib,
    include_directories: include_directories('../../include'),
    dependencies: cwyrm_lib_dep,
)

install_headers('../../include/wy.h')
