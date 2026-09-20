/* Nanosecond mtimes (st_mtim) need POSIX 2008; defined before any include
 * so a source touched in the same second the cache was written still
 * invalidates it. */
#define _POSIX_C_SOURCE 200809L

#include <wyrm/platform/hosted/import_cache.h>

#include <wyrm/sys/string.h>
#include <wyrm/util.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Epic 11 M3: the .wyd source cache, ported from pypoc/wypoc/cache.py's
 * image half. Same contract: a cache file is valid iff it is not older
 * than its source (mtime), and every I/O failure is silently skipped -
 * the cache is best-effort, never fatal. Cache lookups consider only
 * .wyd, so the Python and wyrm toolchains never consume each other's
 * caches. */

#define WY_CACHE_PATH_MAX 4096

bool wy_import_cache_source_mtime(const char* path, double* out_mtime)
{
    if (path == WY_NULL || out_mtime == WY_NULL) { return false; }
    struct stat st;
    if (stat(path, &st) != 0) { return false; }
    if (!S_ISREG(st.st_mode)) { return false; }
    /* Nanosecond precision: a whole-second mtime cannot tell a cache
     * written at second X from a source touched at second X, and a touch
     * must always invalidate. */
    *out_mtime = (double) st.st_mtim.tv_sec + (double) st.st_mtim.tv_nsec / 1e9;
    return true;
}

/* "<dir>/<name>" from an arbitrary path; `dir_out`/`name_out` are pointers
 * into `text` (a '/' replaced with '\0' for the dir half). */
static void split_dir_name_(char* text, char** dir_out, char** name_out)
{
    char* last_slash = strrchr(text, '/');
    if (last_slash == WY_NULL) {
        *dir_out = WY_NULL;
        *name_out = text;
        return;
    }
    if (last_slash == text) {
        /* "/f.wy": the directory is the root "/" */
        if (last_slash[1] != '\0') { *dir_out = text; last_slash[1] = '\0'; }
        else { *dir_out = WY_NULL; }
        *name_out = last_slash + 1;
        return;
    }
    *dir_out = text;
    *last_slash = '\0';
    *name_out = last_slash + 1;
}

static bool path_is_absolute_(const char* path)
{
    return path != WY_NULL && path[0] == '/';
}

char* wy_import_cache_path(wy_allocator* allocator, const char* cache_dir, const char* source_path)
{
    if (allocator == WY_NULL || source_path == WY_NULL) { return WY_NULL; }

    char abs[WY_CACHE_PATH_MAX];
    size_t at = 0;
    abs[0] = '\0';
    if (path_is_absolute_(source_path)) {
        at = strlen(source_path);
        if (at >= sizeof(abs)) { return WY_NULL; }
        memcpy(abs, source_path, at + 1);
    } else {
        char cwd[WY_CACHE_PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == WY_NULL) { return WY_NULL; }
        at = strlen(cwd);
        if (at + 1 + strlen(source_path) >= sizeof(abs)) { return WY_NULL; }
        memcpy(abs, cwd, at);
        abs[at++] = '/';
        memcpy(abs + at, source_path, strlen(source_path) + 1);
        at += strlen(source_path);
    }

    /* abs is now the absolute source path; split it in place. */
    char* dir = WY_NULL;
    char* name = WY_NULL;
    split_dir_name_(abs, &dir, &name);

    /* /dir/foo.wy caches as foo.wyd: drop the trailing ".wy" (the cache
     * extension carries the provenance, ".wy" the fact it is a source). */
    size_t name_len = strlen(name);
    if (name_len > 3 && memcmp(name + name_len - 3, ".wy", 3) == 0) { name_len -= 3; }
    char* result;
    size_t prefix_len;
    if (cache_dir != WY_NULL) {
        prefix_len = strlen(cache_dir);
        /* <cache_dir>/<abs dir>/<name>.wyd. The abs dir always begins
         * with '/', so it appends directly to the prefix (no doubled
         * separator, no __wycache__ component). */
        size_t dir_len = (dir != WY_NULL ? strlen(dir) : 0);
        result = (char*) wy_allocator_alloc(allocator,
            prefix_len + dir_len + 1 + name_len + strlen(WY_IMPORT_CACHE_EXT) + 1);
        if (result == WY_NULL) { return WY_NULL; }
        char* write = result;
        memcpy(write, cache_dir, prefix_len); write += prefix_len;
        if (dir != WY_NULL) {
            memcpy(write, dir, dir_len); write += dir_len;
        }
        *write++ = '/';
        memcpy(write, name, name_len); write += name_len;
        memcpy(write, WY_IMPORT_CACHE_EXT, strlen(WY_IMPORT_CACHE_EXT) + 1);
        return result;
    }

    /* Default: <dir>/__wycache__/<name>.wyd. */
    size_t dir_len = (dir != WY_NULL ? strlen(dir) : 0);
    size_t marker_len = strlen(WY_IMPORT_CACHE_DIR_NAME);
    result = (char*) wy_allocator_alloc(allocator,
        dir_len + 1 + marker_len + 1 + name_len + strlen(WY_IMPORT_CACHE_EXT) + 1);
    if (result == WY_NULL) { return WY_NULL; }
    char* write = result;
    if (dir != WY_NULL) {
        memcpy(write, dir, dir_len); write += dir_len;
        *write++ = '/';
    }
    memcpy(write, WY_IMPORT_CACHE_DIR_NAME, marker_len); write += marker_len;
    *write++ = '/';
    memcpy(write, name, name_len); write += name_len;
    memcpy(write, WY_IMPORT_CACHE_EXT, strlen(WY_IMPORT_CACHE_EXT) + 1);
    return result;
}

bool wy_import_cache_valid(const char* cache_path, double source_mtime)
{
    if (cache_path == WY_NULL) { return false; }
    struct stat st;
    if (stat(cache_path, &st) != 0) { return false; }
    if (!S_ISREG(st.st_mode)) { return false; }
    double cache_mtime = (double) st.st_mtim.tv_sec + (double) st.st_mtim.tv_nsec / 1e9;
    return cache_mtime >= source_mtime;  /* "not older than" */
}

/* mkdir -p: creates `dir` and every intermediate (dir is a modifiable copy). */
static bool mkdir_p_(char* dir)
{
    size_t len = strlen(dir);
    for (size_t i = 1; i < len; i++) {
        if (dir[i] != '/') { continue; }
        dir[i] = '\0';
        if (mkdir(dir, 0777) != 0 && errno != EEXIST) { dir[i] = '/'; return false; }
        dir[i] = '/';
    }
    if (len == 0 || (mkdir(dir, 0777) != 0 && errno != EEXIST)) { return len == 0; }
    return true;
}

bool wy_import_cache_ensure_dir(const char* dir)
{
    if (dir == WY_NULL || dir[0] == '\0') { return false; }
    char work[WY_CACHE_PATH_MAX];
    if (strlen(dir) >= sizeof(work)) { return false; }
    memcpy(work, dir, strlen(dir) + 1);
    return mkdir_p_(work);
}

bool wy_import_cache_store(wy_allocator* allocator, const char* cache_path,
    const wy_u8* bytes, wy_uword len, const char** reason)
{
    if (allocator == WY_NULL || cache_path == WY_NULL ||
        (bytes == WY_NULL && len > 0)) {
        if (reason != WY_NULL) { *reason = "bad argument"; }
        return false;
    }

    size_t work_len = strlen(cache_path) + 1;
    char* work = (char*) wy_allocator_alloc(allocator, work_len);
    if (work == WY_NULL) {
        if (reason != WY_NULL) { *reason = "out of memory"; }
        return false;
    }
    memcpy(work, cache_path, work_len);

    char* dir = WY_NULL;
    char* name = WY_NULL;
    split_dir_name_(work, &dir, &name);
    bool made = true;
    if (dir != WY_NULL) {
        made = mkdir_p_(dir);  /* dir is the prefix of `work` */
    }
    if (!made) {
        if (reason != WY_NULL) { *reason = "mkdir failed"; }
        wy_allocator_free(allocator, work);
        return false;
    }

    FILE* fp = fopen(cache_path, "wb");
    if (fp == WY_NULL) {
        if (reason != WY_NULL) { *reason = "open failed"; }
        wy_allocator_free(allocator, work);
        return false;
    }
    bool ok = (len == 0) || (fwrite(bytes, 1, (size_t) len, fp) == (size_t) len);
    if (fclose(fp) != 0) { ok = false; }
    if (!ok) {
        if (reason != WY_NULL) { *reason = "write failed"; }
        remove(cache_path);  /* never leave a partial cache behind */
    }
    wy_allocator_free(allocator, work);
    return ok;
}
