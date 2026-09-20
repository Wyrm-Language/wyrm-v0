#include "io_native.h"

#include <wyrm/builtins.h>
#include <wyrm/bytes.h>
#include <wyrm/error.h>
#include <wyrm/exec_fn.h>
#include <wyrm/fiber.h>
#include <wyrm/machine.h>
#include <wyrm/native.h>
#include <wyrm/slot.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/value.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Shared helpers
 * ------------------------------------------------------------------------- */

/**
 * Copy a wy_string (not necessarily NUL-terminated) into a bounded stack
 * buffer for a POSIX call that needs a C string. Real filesystem paths are
 * always far under this bound; a longer one is reported as an OSError
 * rather than silently truncated.
 */
enum { WY_IO_PATH_BUF = 4096 };

static bool copy_cstr_(wy_string* s, char* buf, wy_uword buf_size)
{
    if (s == WY_NULL || s->len >= buf_size) { return false; }
    wy_memcpy(buf, s->str, s->len);
    buf[s->len] = '\0';
    return true;
}

/**
 * Descriptors opened in binary mode ("rb", "wb", "r+b", ...). `__read` answers
 * a bytes value for these and a str otherwise, which is how the one std/io.wy
 * (written for pypoc, whose `__read` behaves the same way) implements
 * `read_bytes` as `bytes(__read(handle, n))`. A bitmap over the low fds; a
 * descriptor above the range is simply treated as text mode.
 */
enum { WY_IO_BINARY_FDS = 1024 };
static wy_u8 io_binary_fds_[WY_IO_BINARY_FDS / 8];

static void io_set_binary_(int fd, bool binary)
{
    if (fd < 0 || fd >= WY_IO_BINARY_FDS) { return; }
    if (binary) { io_binary_fds_[fd / 8] |= (wy_u8) (1u << (fd % 8)); }
    else { io_binary_fds_[fd / 8] &= (wy_u8) ~(1u << (fd % 8)); }
}

static bool io_is_binary_(wy_word fd)
{
    if (fd < 0 || fd >= WY_IO_BINARY_FDS) { return false; }
    return (io_binary_fds_[fd / 8] >> (fd % 8)) & 1u;
}

/** `mode` is an open()-style mode string ("r", "w", "a", "r+", "w+", "a+",
 * with an optional trailing "b" ignored - there is no text/binary
 * distinction at the POSIX layer), matching pypoc/wypoc/wyrm_io.py. */
static bool mode_to_flags_(const char* mode, wy_uword len, int* out_flags)
{
    if (len == 0) { return false; }
    if (len > 1 && mode[len - 1] == 'b') { len--; }
    if (len == 1 && mode[0] == 'r') { *out_flags = O_RDONLY; return true; }
    if (len == 1 && mode[0] == 'w') { *out_flags = O_WRONLY | O_CREAT | O_TRUNC; return true; }
    if (len == 1 && mode[0] == 'a') { *out_flags = O_WRONLY | O_CREAT | O_APPEND; return true; }
    if (len == 2 && mode[1] == '+') {
        if (mode[0] == 'r') { *out_flags = O_RDWR; return true; }
        if (mode[0] == 'w') { *out_flags = O_RDWR | O_CREAT | O_TRUNC; return true; }
        if (mode[0] == 'a') { *out_flags = O_RDWR | O_CREAT | O_APPEND; return true; }
    }
    return false;
}

/** Build an OSError value from `errno` - never a VM fault: the reference
 * `open(path, mode)` in corelib/std/io.wy is documented to answer an error
 * *value* on failure, propagated with `try`, not to abort the fiber. */
static wy_error os_error_value_(wy_context* context, int err_no, wy_value* out)
{
    wy_string* what = WY_NULL;
    const char* msg = strerror(err_no);
    wy_error err = wy_string_strdup(context, msg, &what);
    if (err != WY_ERR_NONE) { return err; }
    wy_error_obj* obj = WY_NULL;
    err = wy_error_obj_new(context, context->os_error_class, what, wy_value_nil(), &obj);
    if (err != WY_ERR_NONE) { return err; }
    *out = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
    return WY_ERR_NONE;
}

/** Engine-level fault (bad argument types/counts): distinct from the
 * OSError *values* above, which are ordinary, catchable results. */
static wy_exec_state native_fault_(wy_context* context, const char* message)
{
    wy_string* what = WY_NULL;
    wy_error_obj* obj = WY_NULL;
    if (wy_string_strdup(context, message, &what) == WY_ERR_NONE &&
        wy_error_obj_new(context, context->error_class, what, wy_value_nil(), &obj) == WY_ERR_NONE) {
        context->current_fiber->fault = wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) obj);
    } else {
        context->current_fiber->fault = wy_value_word(-1);
    }
    return WY_EXEC_FAULT;
}

static bool arg_word_(wy_context* context, wy_uword index, wy_word* out)
{
    wy_value v = *wy_fiber_value_n(context->current_fiber, index);
    if (v.type != WY_TYPE_TAG_WORD) { return false; }
    *out = v.data.word;
    return true;
}

static bool arg_str_(wy_context* context, wy_uword index, wy_string** out)
{
    wy_value v = *wy_fiber_value_n(context->current_fiber, index);
    if (v.type != WY_TYPE_TAG_STR) { return false; }
    *out = (wy_string*) v.data.gc_object;
    return true;
}

/**
 * Epic 7 M4: `write`'s data argument may be a str or a bytes value -
 * whichever mode the handle was opened in ("w"/"a" for str, "wb"/"ab" for
 * bytes), matching pypoc's corelib/std/io.wy File.write. There is no
 * text/binary distinction at the POSIX write(2) layer either way, so both
 * just hand their raw bytes to the same loop.
 */
static bool arg_data_(wy_context* context, wy_uword index, const wy_u8** out_data, wy_uword* out_len)
{
    wy_value v = *wy_fiber_value_n(context->current_fiber, index);
    if (v.type == WY_TYPE_TAG_STR) {
        wy_string* s = (wy_string*) v.data.gc_object;
        *out_data = (const wy_u8*) s->str;
        *out_len = s->len;
        return true;
    }
    if (v.type == WY_TYPE_TAG_BYTES) {
        wy_bytes* b = (wy_bytes*) v.data.gc_object;
        *out_data = b->data;
        *out_len = b->len;
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * The seven natives (pypoc/wypoc/wyrm_io.py's wyrm_open/read/write/lseek/
 * dup2/close/flush, backed by real POSIX fds instead of a synthetic handle
 * table - a real fd already satisfies "0/1/2 are stdin/stdout/stderr").
 * ------------------------------------------------------------------------- */

static wy_exec_state io_open_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_string* path = WY_NULL;
    wy_string* mode_str = WY_NULL;
    if (!arg_str_(context, 0, &path) || !arg_str_(context, 1, &mode_str)) {
        return native_fault_(context, "open: path and mode must be strings");
    }
    char path_buf[WY_IO_PATH_BUF];
    if (!copy_cstr_(path, path_buf, sizeof(path_buf))) {
        return native_fault_(context, "open: path too long or invalid");
    }
    int flags;
    if (!mode_to_flags_(mode_str->str, mode_str->len, &flags)) {
        return native_fault_(context, "open: unsupported mode string");
    }
    int fd = open(path_buf, flags, 0644);
    wy_value result;
    if (fd < 0) {
        if (os_error_value_(context, errno, &result) != WY_ERR_NONE) { return native_fault_(context, "open: out of memory building OSError"); }
    } else {
        io_set_binary_(fd, mode_str->len > 0 && mode_str->str[mode_str->len - 1] == 'b');
        result = wy_value_word(fd);
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

/**
 * Shared `read(2)` loop behind both `read` (-> str) and epic 7 M4's
 * `read_bytes` (-> bytes): reads `size` bytes, or to EOF when `size < 0`,
 * into a heap buffer the caller owns on success. On a read() failure this
 * sets the fiber's result to an OSError *value* itself (matching every
 * other io_native.c native's error-value-not-fault contract) and returns
 * false; the caller must return WY_EXEC_DONE immediately without touching
 * the result again.
 */
static bool read_into_buffer_(wy_context* context, wy_word handle, wy_word size, char** out_buf, wy_uword* out_len)
{
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_uword want = (size < 0) ? 0 : (wy_uword) size;
    bool read_all = size < 0;
    enum { WY_IO_READ_CHUNK = 4096 };
    wy_uword cap = read_all ? WY_IO_READ_CHUNK : want;
    if (cap == 0) { cap = 1; }
    char* buf = (char*) wy_allocator_alloc(allocator, cap);
    if (buf == WY_NULL) { native_fault_(context, "read: out of memory"); return false; }

    wy_uword total = 0;
    for (;;) {
        wy_uword target = read_all ? cap : want;
        if (total >= target && !read_all) { break; }
        if (total == cap) {
            wy_uword new_cap = cap * 2;
            char* grown = (char*) wy_allocator_realloc(allocator, buf, new_cap);
            if (grown == WY_NULL) { wy_allocator_free(allocator, buf); native_fault_(context, "read: out of memory"); return false; }
            buf = grown;
            cap = new_cap;
        }
        ssize_t n = read((int) handle, buf + total, cap - total);
        if (n < 0) {
            int err_no = errno;
            wy_allocator_free(allocator, buf);
            wy_value result;
            if (os_error_value_(context, err_no, &result) != WY_ERR_NONE) {
                native_fault_(context, "read: out of memory building OSError");
                return false;
            }
            wy_context_set_result(context, 0, result);
            return false;
        }
        if (n == 0) { break; }
        total += (wy_uword) n;
        if (!read_all && total >= want) { break; }
    }
    *out_buf = buf;
    *out_len = total;
    return true;
}

/**
 * `__read(handle, size)`: `size` bytes, or everything to EOF when negative.
 * A str for a text-mode handle, a bytes value for one opened with a "b" mode.
 */
static wy_exec_state io_read_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle, size;
    if (!arg_word_(context, 0, &handle) || !arg_word_(context, 1, &size)) {
        return native_fault_(context, "read: handle and size must be integers");
    }
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    char* buf = WY_NULL;
    wy_uword total = 0;
    if (!read_into_buffer_(context, handle, size, &buf, &total)) { return WY_EXEC_DONE; }

    wy_value result;
    if (io_is_binary_(handle)) {
        wy_bytes* result_bytes = WY_NULL;
        wy_error err = wy_bytes_new(context, (const wy_u8*) buf, total, &result_bytes);
        wy_allocator_free(allocator, buf);
        if (err != WY_ERR_NONE) { return native_fault_(context, "read: out of memory building result"); }
        result = wy_value_object(WY_TYPE_TAG_BYTES, (wy_object*) result_bytes);
    } else {
        wy_string* result_str = WY_NULL;
        wy_error err = wy_string_new(context, buf, total, &result_str);
        wy_allocator_free(allocator, buf);
        if (err != WY_ERR_NONE) { return native_fault_(context, "read: out of memory building result"); }
        result = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) result_str);
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

static wy_exec_state io_write_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle;
    const wy_u8* data = WY_NULL;
    wy_uword data_len = 0;
    if (!arg_word_(context, 0, &handle) || !arg_data_(context, 1, &data, &data_len)) {
        return native_fault_(context, "write: handle must be an integer, data a string or bytes");
    }
    /* stdout goes through the context's output hook when the host set one -
     * the same sink the bare `println` builtin uses - so an embedder (or a
     * test) that captures output sees File/println writes too. Without a
     * hook it is the real descriptor. */
    if (handle == 1 && context->io.write != WY_NULL) {
        context->io.write(context, (const char*) data, data_len, context->io.ud);
        wy_context_set_result(context, 0, wy_value_word((wy_word) data_len));
        return WY_EXEC_DONE;
    }
    wy_uword total = 0;
    while (total < data_len) {
        ssize_t n = write((int) handle, data + total, data_len - total);
        if (n < 0) {
            wy_value result;
            if (os_error_value_(context, errno, &result) != WY_ERR_NONE) { return native_fault_(context, "write: out of memory building OSError"); }
            wy_context_set_result(context, 0, result);
            return WY_EXEC_DONE;
        }
        total += (wy_uword) n;
    }
    wy_context_set_result(context, 0, wy_value_word((wy_word) total));
    return WY_EXEC_DONE;
}

static wy_exec_state io_lseek_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle, offset, whence;
    if (!arg_word_(context, 0, &handle) || !arg_word_(context, 1, &offset) || !arg_word_(context, 2, &whence)) {
        return native_fault_(context, "lseek: handle, offset, and whence must be integers");
    }
    off_t pos = lseek((int) handle, (off_t) offset, (int) whence);
    wy_value result;
    if (pos < 0) {
        if (os_error_value_(context, errno, &result) != WY_ERR_NONE) { return native_fault_(context, "lseek: out of memory building OSError"); }
    } else {
        result = wy_value_word((wy_word) pos);
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

static wy_exec_state io_dup2_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word old_handle, new_handle;
    if (!arg_word_(context, 0, &old_handle) || !arg_word_(context, 1, &new_handle)) {
        return native_fault_(context, "dup2: old and new must be integers");
    }
    int rc = dup2((int) old_handle, (int) new_handle);
    wy_value result;
    if (rc < 0) {
        if (os_error_value_(context, errno, &result) != WY_ERR_NONE) { return native_fault_(context, "dup2: out of memory building OSError"); }
    } else {
        result = wy_value_word(rc);
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

static wy_exec_state io_close_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle;
    if (!arg_word_(context, 0, &handle)) { return native_fault_(context, "close: handle must be an integer"); }
    int rc = close((int) handle);
    io_set_binary_((int) handle, false);
    wy_value result;
    if (rc < 0) {
        if (os_error_value_(context, errno, &result) != WY_ERR_NONE) { return native_fault_(context, "close: out of memory building OSError"); }
    } else {
        result = wy_value_word(0);
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

static wy_exec_state io_flush_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle;
    if (!arg_word_(context, 0, &handle)) { return native_fault_(context, "flush: handle must be an integer"); }
    /* A raw fd has no user-space buffering to flush; fsync forces the OS to
     * persist what has already been written. EINVAL/ENOTSUP (pipes, ttys,
     * sockets) is not a real failure of "flush" for a stream with no
     * durability concept, so it is swallowed rather than surfaced. */
    wy_value result = wy_value_word(0);
    if (fsync((int) handle) < 0 && errno != EINVAL && errno != ENOTSUP) {
        if (os_error_value_(context, errno, &result) != WY_ERR_NONE) { return native_fault_(context, "flush: out of memory building OSError"); }
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

/* -------------------------------------------------------------------------
 * Module assembly
 * ------------------------------------------------------------------------- */

typedef struct io_exec_entry_
{
    const char* name;
    wy_u8 min_argc;
    wy_u8 max_argc;
    wy_exec_fn_c_call fn;
} io_exec_entry_;

/* The names are pypoc's own builtins (wypoc/wyrm_builtins.py), which is what
 * the one std/io.wy is written against. */
static const io_exec_entry_ io_exec_natives_[] = {
    { "__open",  2, 2, io_open_exec_ },
    { "__read",  2, 2, io_read_exec_ },
    { "__write", 2, 2, io_write_exec_ },
    { "__lseek", 3, 3, io_lseek_exec_ },
    { "__dup2",  2, 2, io_dup2_exec_ },
    { "__close", 1, 1, io_close_exec_ },
    { "__flush", 1, 1, io_flush_exec_ },
};

static const struct { const char* name; wy_word value; } io_consts_[] = {
    { "__STDIN", 0 }, { "__STDOUT", 1 }, { "__STDERR", 2 },
};

enum { WY_IO_EXEC_COUNT = sizeof(io_exec_natives_) / sizeof(io_exec_natives_[0]),
    WY_IO_CONST_COUNT = sizeof(io_consts_) / sizeof(io_consts_[0]),
    WY_IO_GLOBAL_COUNT = WY_IO_EXEC_COUNT + WY_IO_CONST_COUNT };

wy_error wy_io_module_new(wy_context* context, wy_module** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }
    module->state = WY_MODULE_BUILTIN;

    module->global_count = WY_IO_GLOBAL_COUNT;
    module->globals = wy_context_gc_alloc(context, sizeof(wy_value) * WY_IO_GLOBAL_COUNT);
    module->fill_layer = wy_context_gc_alloc(context, sizeof(wy_u8) * WY_IO_GLOBAL_COUNT);
    module->fill_source = wy_context_gc_alloc(context, sizeof(wy_symbol) * WY_IO_GLOBAL_COUNT);
    if (module->globals == WY_NULL || module->fill_layer == WY_NULL || module->fill_source == WY_NULL) {
        return WY_ERR_NOMEM;
    }
    for (wy_uword i = 0; i < WY_IO_GLOBAL_COUNT; i++) {
        module->globals[i] = wy_value_unset();
        module->fill_layer[i] = 0;
        module->fill_source[i] = WY_NULL;
    }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_error err = wy_slot_dict_expand_f(&module->exports, allocator, WY_IO_GLOBAL_COUNT * 2);
    if (err != WY_ERR_NONE) { return err; }

    wy_uword slot = 0;
    for (wy_uword i = 0; i < WY_IO_EXEC_COUNT; i++) {
        const io_exec_entry_* entry = &io_exec_natives_[i];
        wy_symbol sym;
        err = wy_context_intern(context, entry->name, wy_strlen_f(entry->name), &sym);
        if (err != WY_ERR_NONE) { return err; }
        wy_native* native = WY_NULL;
        err = wy_native_exec_new(context, sym, entry->min_argc, entry->max_argc,
            wy_exec_fn_create(entry->fn, wy_primitive_null()), &native);
        if (err != WY_ERR_NONE) { return err; }
        module->globals[slot] = wy_value_object(WY_TYPE_TAG_NATIVE, (wy_object*) native);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    for (wy_uword i = 0; i < WY_IO_CONST_COUNT; i++) {
        wy_symbol sym;
        err = wy_context_intern(context, io_consts_[i].name, wy_strlen_f(io_consts_[i].name), &sym);
        if (err != WY_ERR_NONE) { return err; }
        module->globals[slot] = wy_value_word(io_consts_[i].value);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    WY_ASSERT(slot == WY_IO_GLOBAL_COUNT);
    *out = module;
    return WY_ERR_NONE;
}

wy_error wy_io_natives_install(wy_context* context)
{
    if (context == WY_NULL || context->builtins == WY_NULL) { return WY_ERR_INVAL; }

    wy_module* holder = WY_NULL;
    wy_error err = wy_io_module_new(context, &holder);
    if (err != WY_ERR_NONE) { return err; }

    for (wy_uword slot = 0; slot < WY_IO_GLOBAL_COUNT; slot++) {
        const char* name = slot < WY_IO_EXEC_COUNT ? io_exec_natives_[slot].name
                                                    : io_consts_[slot - WY_IO_EXEC_COUNT].name;
        err = wy_builtins_add(context, name, holder->globals[slot]);
        if (err != WY_ERR_NONE) { return err; }
    }
    return WY_ERR_NONE;
}
