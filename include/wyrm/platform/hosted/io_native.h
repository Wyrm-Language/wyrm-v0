#ifndef WYRM_PLATFORM_HOSTED_IO_NATIVE_H_
#define WYRM_PLATFORM_HOSTED_IO_NATIVE_H_

#include <wyrm/context.h>
#include <wyrm/module.h>

WY_BEGIN_DECLS

/**
 * Build the `std::io` module (epic_5.md M4): seven POSIX-backed exec
 * natives - `open`/`read`/`write`/`lseek`/`dup2`/`close`/`flush` - plus the
 * `STDIN`/`STDOUT`/`STDERR` handle constants. Handles are real POSIX file
 * descriptors (design's "handles are small integers, POSIX-style"); 0/1/2
 * already are stdin/stdout/stderr, so no separate handle table is needed
 * the way the reference wyrm_io.py keeps one.
 *
 * A syscall failure never faults the fiber: it writes an `OSError` value as
 * the ordinary return (matching pypoc/wypoc/corelib/std/io.wy's "the handle
 * is an error value when the open fails" contract, propagated by `try`).
 */
wy_error wy_io_module_new(wy_context* context, wy_module** out);

/**
 * Build the module (wy_io_module_new) and register it under the import
 * path "std::io" (wy_context_module_register), so `import std::io` resolves
 * it directly - wy_link_import checks already-registered modules by name
 * before ever consulting context->import_hook.
 */
wy_error wy_io_module_install(wy_context* context);

WY_END_DECLS

#endif
