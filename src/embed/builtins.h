#ifndef WYRM_EMBEDDED_BUILTINS_H_
#define WYRM_EMBEDDED_BUILTINS_H_

#include <wyrm/platform/hosted/import_fs.h>

/* Epic 11 M1: the builtin module table - the ported compiler plus the
 * library modules it needs, as static wy_module_image C sources. They are
 * build output, never committed: wy/wyrm/tools/embed_build.wy writes them, run
 * by scripts/embed_stage.py (see src/embed/meson.build). The selfcompile test
 * checks that this binary reproduces them.
 *
 * Resolution order: -I roots first (a disk module shadows a builtin), then
 * this table - the expansion VM inherits it through the parent's
 * import_hook/import_ud, which is how std::expand's child finds
 * wyrm::compiler::expand with no file on disk. std::io is in the table (its
 * `__open`/`__read`/... natives, src/embed/std/io_native.c, are added to the
 * builtins of contexts the host chooses to extend - never an expansion VM,
 * whose import hook also refuses `std::io`). std::expand is a host module
 * (src/wyrm/main.c) and is deliberately absent from the table. */

extern const wy_import_fs_builtin wyrm_builtin_modules[];
extern const wy_uword wyrm_builtin_module_count;

#endif
