#ifndef WYRM_EMBED_STD_IO_NATIVE_H_
#define WYRM_EMBED_STD_IO_NATIVE_H_

#include <wyrm/context.h>
#include <wyrm/module.h>

WY_BEGIN_DECLS

/*
 * Native support for the embedded std/io.wy. The .wy module (File, open,
 * read_file, println, ...) is written against pypoc's own builtins; this
 * file supplies the same builtins on the C VM, over real POSIX descriptors:
 *
 *   __open(path, mode)  __read(fd, size)  __write(fd, data)  __lseek(fd, off, whence)
 *   __dup2(old, new)    __close(fd)       __flush(fd)
 *   __STDIN  __STDOUT  __STDERR            (0, 1, 2)
 *
 * Handles are the descriptors themselves (0/1/2 already are stdin/stdout/
 * stderr). A syscall failure never faults the fiber: it answers an OSError
 * *value*, propagated by `try`, exactly as in pypoc. `__read` answers a
 * bytes value for a descriptor opened with a "b" mode, else a str.
 */

/**
 * Build a module object holding the natives above under their `__` names.
 * Used by wy_io_natives_install and by tests that drive the natives directly.
 */
wy_error wy_io_module_new(wy_context* context, wy_module** out);

/**
 * Append the natives to `context->builtins` (wy_builtins_add), so any module
 * linked afterwards - the embedded std::io in particular - resolves them as
 * bare names. Host-chosen: a context that is not extended (an expansion VM)
 * cannot do I/O even if it imports std::io.
 */
wy_error wy_io_natives_install(wy_context* context);

WY_END_DECLS

#endif
