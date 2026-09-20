# Modules compiled into the wyrm executable (and into test_cwyrm), not into
# libcwyrm: the generated builtin table plus the hand-written native support
# some of them need. Layout mirrors the wyrm package - each module's generated
# `<name>.c` image sits beside its `<name>.wy` source, and its `<name>_native.c`
# (when it has one) beside both. The images are pulled in by the unity file
# embedded_images.c, so only the two generated root files are listed here.
embed_sources = files('embedded_images.c', 'wyrm_builtins_table.c')
embed_inc = include_directories('.')

if get_option('hosted')
    # std::io's POSIX natives (open/read/write/lseek/... on real descriptors).
    embed_sources += files('std/io_native.c')
endif
