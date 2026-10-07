#ifndef WYRM_PLATFORM_HOSTED_IMPORT_FS_H_
#define WYRM_PLATFORM_HOSTED_IMPORT_FS_H_

#include <wyrm/allocator.h>
#include <wyrm/context.h>
#include <wyrm/image.h>

WY_BEGIN_DECLS

/**
 * One builtin module table row (epic 11): a virtual import path and the
 * static `wy_module_image` served for it. The image must be static storage
 * (or otherwise outlive every context that loads it): loading is
 * wy_module_load_image, zero-copy.
 */
typedef struct wy_import_fs_builtin
{
    const char* path;              /**< virtual import path, `"a::b::c"` */
    const wy_module_image* image;  /**< static image answered for `path` */
} wy_import_fs_builtin;

/**
 * A filesystem module search path (the `-I dir` roots of the CLI), plus the
 * builtin module table always consulted last (epic 11's resolution order:
 * per root - `<path>/__init__` then `<path>`, each as .wy source through the
 * cache, then .wyd, then .wyc - then the table; a disk module shadows a
 * builtin; a bare directory or table rows under the path make a namespace
 * package, answered only when nothing else is).
 *
 * Owns its strings and its root table, both allocated through the
 * `wy_allocator` handed to wy_import_fs_add_root. The table is not owned -
 * it points at static rows, typically the generated `wyrm_builtin_modules`
 * (src/wyrm/embedded/). Pass its address as the `ud` argument to
 * wy_import_fs_hook via `context->import_ud`; hosts that copy the hook into
 * another context (the expansion VM) copy the table and cache config with
 * it.
 */
typedef struct wy_import_fs_search_path
{
    const char** roots;
    wy_uword count;
    wy_uword capacity;
    const wy_import_fs_builtin* builtins; /**< last resolution step; may be NULL */
    wy_uword builtin_count;
    const char* cache_dir;     /**< --cache-dir prefix; NULL = __wycache__ beside sources */
    bool verbose;              /**< -v: one stderr line per resolution */
    /**
     * Compiles a .wy source to container bytes (the embedded compiler; the
     * CLI provides it - a hook may not call back into the *requesting* VM,
     * so the implementation runs the compiler on an independent machine).
     * Called for a root's `<path>.wy` hit on a cache miss; NULL disables
     * .wy source resolution entirely (the pre-M3 loader behavior).
     */
    wy_error (*compile)(void* ud, wy_context* requester, const char* source_path,
        const wy_u8* source, wy_uword source_len,
        wy_u8** out_bytes, wy_uword* out_len, const char** msg);
    void* compile_ud;
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
 * `len` bytes) under each root in order, first hit wins. Within a root a
 * package beats a module (design/modules.md M1, Python's order): the steps
 * below are tried at `<base>/__init__` when `<base>` is a directory, then
 * at `<base>` itself, where `<base>` is `<root>/<path-with-slashes>`.
 *
 *   1. `<root>/<path-with-slashes>.wy` - a source file, compiled through
 *      the .wyd cache (epic 11 M3): a valid `<dir>/__wycache__/<name>.wyd`
 *      (or `--cache-dir` prefix) is served as bytes; a miss calls the
 *      search path's `compile` hook (if set) and stores the result
 *      best-effort. Any cache I/O failure is silently skipped.
 *   2. `<root>/<path-with-slashes>.wyd` - a precompiled port-built image.
 *   3. `<root>/<path-with-slashes>.wyc` - a precompiled pypoc image.
 *
 * .wyd comes before .wyc so a stale pypoc artifact never shadows a fresh
 * port build in a mixed tree. After the last root, the search path's
 * builtin table is scanned (epic 11): a hit is answered through `out_image`
 * as a static image, leaving `out_bytes` untouched. Failing that, a path
 * that is a directory on some root, or that has table rows filed under it
 * (`path::...`), is a namespace package: WY_ERR_NONE with neither output
 * set (see wy_import_hook).
 *
 * On the bytes path the bytes are allocated through wy_context_gc_alloc and
 * ownership transfers to the loader (also on a malformed image), matching
 * wy_link_import's contract. A missing module returns WY_ERR_UNBOUND; a
 * .wy source that fails to compile returns the compile hook's error (with
 * a diagnostic on stderr).
 */
wy_error wy_import_fs_hook(wy_context* context, const char* path, wy_uword len,
    wy_u8** out_bytes, wy_uword* out_len, const wy_module_image** out_image, void* ud);

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
