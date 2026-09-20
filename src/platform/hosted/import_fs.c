#include <wyrm/platform/hosted/import_fs.h>

#include <wyrm/machine.h>
#include <wyrm/sys/string.h>
#include <wyrm/util.h>

#include <stdio.h>
#include <string.h>

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

wy_error wy_import_fs_hook(wy_context* context, const char* path, wy_uword len,
    wy_u8** out_bytes, wy_uword* out_len, void* ud)
{
    if (context == WY_NULL || path == WY_NULL || out_bytes == WY_NULL || out_len == WY_NULL) {
        return WY_ERR_INVAL;
    }
    wy_import_fs_search_path* search = (wy_import_fs_search_path*) ud;
    if (search == WY_NULL) { return WY_ERR_UNBOUND; }

    /* A "::" separator becomes a "/", so the converted path never grows. */
    wy_uword slash_len = len;
    for (wy_uword i = 0; i + 1 < len; i++) {
        if (path[i] == ':' && path[i + 1] == ':') { slash_len--; }
    }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    for (wy_uword root_index = 0; root_index < search->count; root_index++) {
        const char* root = search->roots[root_index];
        wy_uword root_len = wy_strlen_f(root);
        /* root + '/' + converted path + ".wyc" + NUL */
        wy_uword need = root_len + 1 + slash_len + 4 + 1;
        char* candidate = (char*) wy_allocator_alloc(allocator, need);
        if (candidate == WY_NULL) { return WY_ERR_NOMEM; }

        char* write = candidate;
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
        wy_memcpy(write, ".wyc", 5);  /* includes the terminating NUL */

        wy_uword file_size = 0;
        wy_u8* data = wy_import_fs_read_file(context, candidate, &file_size);
        wy_allocator_free(allocator, candidate);
        if (data != WY_NULL) {
            *out_bytes = data;
            *out_len = file_size;
            return WY_ERR_NONE;
        }
    }
    return WY_ERR_UNBOUND;
}