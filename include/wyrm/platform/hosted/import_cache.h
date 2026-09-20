#ifndef WYRM_PLATFORM_HOSTED_IMPORT_CACHE_H_
#define WYRM_PLATFORM_HOSTED_IMPORT_CACHE_H_

#include <wyrm/allocator.h>

WY_BEGIN_DECLS

/** The cache directory created beside a source when no --cache-dir is set. */
#define WY_IMPORT_CACHE_DIR_NAME "__wycache__"

/** Cache containers are always .wyd (epic 11: a Python-produced .wyc cache
 * is ignored, so the two toolchains never consume each other's caches). */
#define WY_IMPORT_CACHE_EXT ".wyd"

/**
 * `path`'s mtime, for the cache validity check. Any stat failure answers
 * false (the cache is best-effort and never fatal).
 */
bool wy_import_cache_source_mtime(const char* path, double* out_mtime);

/**
 * The cache container path for `source_path`:
 *   - default: `<dir of source>/__wycache__/<name>.wyd`
 *   - `cache_dir` set: `<cache_dir>/<abs dir of source>/<name>.wyd` - the
 *     source's directory appended to the prefix, no __wycache__ component
 *     (relative source paths are made absolute against the cwd first)
 * The result is allocated through `allocator`; WY_NULL on allocation
 * failure only - path computation itself cannot fail.
 */
char* wy_import_cache_path(wy_allocator* allocator, const char* cache_dir, const char* source_path);

/**
 * Whether `cache_path` is a valid hit: the file exists and its mtime is
 * not older than `source_mtime`. Equal mtimes are valid. Any stat failure
 * answers false.
 */
bool wy_import_cache_valid(const char* cache_path, double source_mtime);

/**
 * Best-effort store: creates intermediate directories, then writes the
 * container. Answers false instead of failing the caller; `*reason` (when
 * non-NULL) names the I/O failure for -v ("mkdir failed", "open failed",
 * "write failed").
 */
bool wy_import_cache_store(wy_allocator* allocator, const char* cache_path,
    const wy_u8* bytes, wy_uword len, const char** reason);

/**
 * mkdir -p: create `dir` and every intermediate (0777, EEXIST tolerated).
 * False when a component cannot be created. Best-effort helper, also used
 * by --build-bc's -o directory.
 */
bool wy_import_cache_ensure_dir(const char* dir);

WY_END_DECLS

#endif
