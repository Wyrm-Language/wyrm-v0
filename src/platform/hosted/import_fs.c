#include <wyrm/platform/hosted/import_fs.h>
#include <wyrm/platform/hosted/import_cache.h>

#include <wyrm/machine.h>
#include <wyrm/sys/string.h>
#include <wyrm/util.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void wy_import_fs_search_path_init_s(wy_import_fs_search_path* self)
{
    wy_memset(self, 0, sizeof(*self));
}

wy_error wy_import_fs_add_root(wy_allocator* allocator, wy_import_fs_search_path* self, const char* root)
{
    if (allocator == WY_NULL || self == WY_NULL || root == WY_NULL) { return WY_ERR_INVAL; }
    if (self->count >= WY_MAX_ARRAY_LEN / sizeof(const char*)) { return WY_ERR_RANGE; }
    if (self->count == self->capacity) {
        wy_uword new_capacity = self->capacity ? self->capacity * 2 : 4;
        const char** new_roots = (const char**) wy_allocator_realloc(allocator,
            (void*) self->roots, new_capacity * sizeof(const char*));
        if (new_roots == WY_NULL) { return WY_ERR_NOMEM; }
        self->roots = new_roots;
        self->capacity = new_capacity;
    }

    wy_uword root_len = wy_strlen_f(root) + 1;
    char* copy = (char*) wy_allocator_alloc(allocator, root_len);
    if (copy == WY_NULL) { return WY_ERR_NOMEM; }
    wy_memcpy(copy, root, root_len);
    self->roots[self->count++] = copy;
    return WY_ERR_NONE;
}

void wy_import_fs_search_path_finalize_f(wy_allocator* allocator, wy_import_fs_search_path* self)
{
    if (self == WY_NULL) { return; }
    if (self->roots != WY_NULL) {
        for (wy_uword i = 0; i < self->count; i++) {
            wy_allocator_free(allocator, (void*) self->roots[i]);
        }
        wy_allocator_free(allocator, (void*) self->roots);
    }
    wy_import_fs_search_path_init_s(self);
}

wy_u8* wy_import_fs_read_file(wy_context* context, const char* path, wy_uword* out_size)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) { return WY_NULL; }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return WY_NULL; }
    long file_size = ftell(fp);
    if (file_size < 0) { fclose(fp); return WY_NULL; }
    rewind(fp);

    wy_u8* data = wy_context_gc_alloc(context, (wy_uword) file_size);
    if (data == WY_NULL && file_size > 0) { fclose(fp); return WY_NULL; }

    if (file_size > 0 && fread(data, (size_t) file_size, 1, fp) != 1) {
        wy_context_gc_free(context, data);
        fclose(fp);
        return WY_NULL;
    }

    fclose(fp);
    *out_size = (wy_uword) file_size;
    return data;
}

static const wy_import_fs_builtin* table_find_(const wy_import_fs_search_path* search,
    const char* path, wy_uword len)
{
    for (wy_uword i = 0; i < search->builtin_count; i++) {
        const wy_import_fs_builtin* row = &search->builtins[i];
        if (row->path == WY_NULL || row->image == WY_NULL) { continue; }
        if (wy_strlen_f(row->path) == len && wy_memcmp(row->path, path, len) == 0) {
            return row;
        }
    }
    return WY_NULL;
}

/** True when some builtin table row lives under `path` (`path::...`):
 * the table's share of a namespace package. */
static bool table_has_children_(const wy_import_fs_search_path* search, const char* path, wy_uword len)
{
    for (wy_uword i = 0; i < search->builtin_count; i++) {
        const wy_import_fs_builtin* row = &search->builtins[i];
        if (row->path == WY_NULL || row->image == WY_NULL) { continue; }
        if (wy_strlen_f(row->path) > len + 2 && wy_memcmp(row->path, path, len) == 0 &&
            row->path[len] == ':' && row->path[len + 1] == ':') {
            return true;
        }
    }
    return false;
}

static bool is_dir_(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * Look for one module file at `base` (no extension): the .wy source through
 * the cache (unless `source_barred`), then .wyd, then .wyc. WY_ERR_NONE with
 * `*found` set on a hit, WY_ERR_NONE with it clear on a miss; anything else
 * is an error the hook returns as is.
 */
static wy_error try_base_(wy_context* context, wy_import_fs_search_path* search, bool source_barred,
    const char* base, wy_uword base_len, wy_u8** out_bytes, wy_uword* out_len, bool* found)
{
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    *found = false;
    /* 1. A .wy source, compiled through the .wyd cache (epic 11 M3).
     * Never for a builtin-table module: its source would need the
     * compiler to compile itself (the table's whole reason for being
     * consulted directly). A precompiled .wyd/.wyc below may still
     * shadow the row - only source lookup is barred. */
    char* source_path = (char*) wy_allocator_alloc(allocator, base_len + 3 + 1);
    if (source_path == WY_NULL) { return WY_ERR_NOMEM; }
    wy_memcpy(source_path, base, base_len);
    wy_memcpy(source_path + base_len, ".wy", 4);
    double source_mtime = 0;
    if (!source_barred && wy_import_cache_source_mtime(source_path, &source_mtime)) {
        char* cache_path = wy_import_cache_path(allocator, search->cache_dir, source_path);
        wy_uword cached_len = 0;
        if (cache_path != WY_NULL && wy_import_cache_valid(cache_path, source_mtime)) {
            *out_bytes = wy_import_fs_read_file(context, cache_path, &cached_len);
            if (*out_bytes != WY_NULL) {
                if (search->verbose) { fprintf(stderr, "wyrm: cache %s\n", cache_path); }
                *out_len = cached_len;
                wy_allocator_free(allocator, cache_path);
                wy_allocator_free(allocator, source_path);
                *found = true;
                return WY_ERR_NONE;
            }
        }
        if (search->compile != WY_NULL) {
            /* Naming the miss for -v: a cache file that exists but is
             * not valid is "stale"; no file is "absent". */
            const char* why = "absent";
            struct stat st;
            if (cache_path != WY_NULL && stat(cache_path, &st) == 0) { why = "stale"; }
            wy_uword source_len = 0;
            wy_u8* source = wy_import_fs_read_file(context, source_path, &source_len);
            if (source != WY_NULL) {
                const char* compile_msg = WY_NULL;
                wy_error err = search->compile(search->compile_ud, context,
                    source_path, source, source_len, out_bytes, out_len, &compile_msg);
                if (err == WY_ERR_NONE) {
                    if (search->verbose) { fprintf(stderr, "wyrm: jit %s (cache %s)\n", source_path, why); }
                    const char* store_reason = WY_NULL;
                    bool stored = cache_path != WY_NULL &&
                        wy_import_cache_store(allocator, cache_path, *out_bytes, *out_len, &store_reason);
                    if (search->verbose) {
                        if (stored) { fprintf(stderr, "wyrm: cache write %s\n", cache_path); }
                        else { fprintf(stderr, "wyrm: cache write skipped (%s)\n", store_reason != WY_NULL ? store_reason : "no cache path"); }
                    }
                    wy_allocator_free(allocator, cache_path);
                    wy_allocator_free(allocator, source_path);
                    *found = true;
                    return WY_ERR_NONE;
                }
                fprintf(stderr, "wyrm: %s: %s\n", source_path,
                    compile_msg != WY_NULL ? compile_msg : "compile failed");
                wy_allocator_free(allocator, cache_path);
                wy_allocator_free(allocator, source_path);
                return err;
            }
        }
        wy_allocator_free(allocator, cache_path);
    }
    wy_allocator_free(allocator, source_path);
    /* 2/3. Precompiled images: .wyd (port) before .wyc (pypoc), so a
     * stale pypoc artifact never shadows a fresh port build. */
    char* candidate = (char*) wy_allocator_alloc(allocator, base_len + 4 + 1);
    if (candidate == WY_NULL) { return WY_ERR_NOMEM; }
    wy_memcpy(candidate, base, base_len);
    static const char* const exts[] = { ".wyd", ".wyc" };
    for (wy_uword ext_index = 0; ext_index < 2; ext_index++) {
        wy_memcpy(candidate + base_len, exts[ext_index], 5);  /* ext + NUL */
        wy_uword file_size = 0;
        wy_u8* data = wy_import_fs_read_file(context, candidate, &file_size);
        if (data != WY_NULL) {
            if (search->verbose) { fprintf(stderr, "wyrm: precompiled %s\n", candidate); }
            wy_allocator_free(allocator, candidate);
            *out_bytes = data;
            *out_len = file_size;
            *found = true;
            return WY_ERR_NONE;
        }
    }
    wy_allocator_free(allocator, candidate);
    return WY_ERR_NONE;
}

wy_error wy_import_fs_hook(wy_context* context, const char* path, wy_uword len,
    wy_u8** out_bytes, wy_uword* out_len, const wy_module_image** out_image, void* ud)
{
    if (context == WY_NULL || path == WY_NULL || out_bytes == WY_NULL || out_len == WY_NULL || out_image == WY_NULL) {
        return WY_ERR_INVAL;
    }
    *out_image = WY_NULL;
    wy_import_fs_search_path* search = (wy_import_fs_search_path*) ud;
    if (search == WY_NULL) { return WY_ERR_UNBOUND; }
    /* A "::" separator becomes a "/", so the converted path never grows. */
    wy_uword slash_len = len;
    for (wy_uword i = 0; i + 1 < len; i++) {
        if (path[i] == ':' && path[i + 1] == ':') { slash_len--; }
    }
    static const char init_name[] = "/__init__";
    const wy_uword init_len = sizeof(init_name) - 1;
    const wy_import_fs_builtin* row = table_find_(search, path, len);
    bool namespace_seen = false;
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    for (wy_uword root_index = 0; root_index < search->count; root_index++) {
        const char* root = search->roots[root_index];
        wy_uword root_len = wy_strlen_f(root);
        /* base = root + '/' + converted path, with room for "/__init__" */
        char* base = (char*) wy_allocator_alloc(allocator, root_len + 1 + slash_len + init_len + 1);
        if (base == WY_NULL) { return WY_ERR_NOMEM; }
        char* write = base;
        wy_memcpy(write, root, root_len);
        write += root_len;
        *write++ = '/';
        for (wy_uword i = 0; i < len; i++) {
            if (path[i] == ':' && i + 1 < len && path[i + 1] == ':') {
                *write++ = '/';
                i++;
            } else {
                *write++ = path[i];
            }
        }
        *write = '\0';
        wy_uword base_len = root_len + 1 + slash_len;
        /* design/modules.md M1, Python's FileFinder order within a root: a
         * package (<path>/__init__) beats a module (<path>), which beats a
         * bare directory (a namespace package, answered only if nothing
         * else anywhere answers the path). */
        bool dir = is_dir_(base);
        bool found = false;
        wy_error err = WY_ERR_NONE;
        if (dir) {
            wy_memcpy(base + base_len, init_name, init_len + 1);
            err = try_base_(context, search, row != WY_NULL, base, base_len + init_len, out_bytes, out_len, &found);
            base[base_len] = '\0';
        }
        if (err == WY_ERR_NONE && !found) {
            err = try_base_(context, search, row != WY_NULL, base, base_len, out_bytes, out_len, &found);
        }
        wy_allocator_free(allocator, base);
        if (err != WY_ERR_NONE || found) { return err; }
        namespace_seen = namespace_seen || dir;
    }

    /* Builtin module table, last (epic 11): a static image answered through
     * out_image; wy_link_import takes it through wy_module_load_image. */
    if (row != WY_NULL) {
        if (search->verbose) { fprintf(stderr, "wyrm: builtin %.*s\n", (int) len, path); }
        *out_image = row->image;
        return WY_ERR_NONE;
    }
    /* A namespace package: a directory on some root, or rows of the table
     * filed under it. Answered with neither output set. */
    if (namespace_seen || table_has_children_(search, path, len)) {
        if (search->verbose) { fprintf(stderr, "wyrm: namespace %.*s\n", (int) len, path); }
        return WY_ERR_NONE;
    }
    return WY_ERR_UNBOUND;
}
