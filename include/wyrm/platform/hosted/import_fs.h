#ifndef WYRM_PLATFORM_HOSTED_IMPORT_FS_H_
#define WYRM_PLATFORM_HOSTED_IMPORT_FS_H_

#include <wyrm/allocator.h>
#include <wyrm/context.h>

WY_BEGIN_DECLS

/**
 * A filesystem module search path (the `-I dir` roots of the CLI).
 *
 * Owns its strings and its root table, both allocated through the
 * `wy_allocator` handed to wy_import_fs_add_root. Pass its address as the
 * `ud` argument to wy_import_fs_hook via `context->import_ud`.
 */
typedef struct wy_import_fs_search_path
{
    const char** roots;
    wy_uword count;
    wy_uword capacity;
} wy_import_fs_search_path;

/** Zero-initialize a search path before the first wy_import_fs_add_root. */
void wy_import_fs_search_path_init_s(wy_import_fs_search_path* self);

/**
 * Append one root directory, duplicating `root` through `allocator`.
 *
 * Roots are searched in the order they were added; the first match wins.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, WY_ERR_RANGE on
 *   overflow, or WY_ERR_NOMEM
 */
wy_error wy_import_fs_add_root(wy_allocator* allocator, wy_import_fs_search_path* self, const char* root);

/** Free every root string and the root table through `allocator`. */
void wy_import_fs_search_path_finalize_f(wy_allocator* allocator, wy_import_fs_search_path* self);

/**
 * The hosted `wy_import_hook`: resolve `path` (a `::`-joined module path,
 * `len` bytes) to `<root>/<path-with-slashes>.wyc` under each root in order.
 * The first existing file wins; there is no `.wy` source-compile fallback.
 *
 * On success the bytes are allocated through wy_context_gc_alloc and
 * ownership transfers to the loader (also on a malformed image), matching
 * wy_link_import's contract. A missing module returns WY_ERR_UNBOUND.
 */
wy_error wy_import_fs_hook(wy_context* context, const char* path, wy_uword len,
    wy_u8** out_bytes, wy_uword* out_len, void* ud);

/**
 * Read a whole file into a wy_context_gc_alloc buffer (never raw malloc), so
 * the module can own and free it the same way it frees everything else it
 * holds. The bytecode loader's preferred entry-file reader.
 *
 * @return the buffer and its size through `out_size`, or WY_NULL (leaving
 *   `out_size` untouched) when the file cannot be opened or read
 */
wy_u8* wy_import_fs_read_file(wy_context* context, const char* path, wy_uword* out_size);

WY_END_DECLS

#endif