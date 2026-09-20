#include <wyrm/platform/hosted/io_native.h>

#include <wyrm/builtins.h>
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
        result = wy_value_word(fd);
    }
    wy_context_set_result(context, 0, result);
    return WY_EXEC_DONE;
}

static wy_exec_state io_read_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle, size;
    if (!arg_word_(context, 0, &handle) || !arg_word_(context, 1, &size)) {
        return native_fault_(context, "read: handle and size must be integers");
    }

    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_uword want = (size < 0) ? 0 : (wy_uword) size;
    bool read_all = size < 0;
    enum { WY_IO_READ_CHUNK = 4096 };
    wy_uword cap = read_all ? WY_IO_READ_CHUNK : want;
    if (cap == 0) { cap = 1; }
    char* buf = (char*) wy_allocator_alloc(allocator, cap);
    if (buf == WY_NULL) { return native_fault_(context, "read: out of memory"); }

    wy_uword total = 0;
    for (;;) {
        wy_uword target = read_all ? cap : want;
        if (total >= target && !read_all) { break; }
        if (total == cap) {
            wy_uword new_cap = cap * 2;
            char* grown = (char*) wy_allocator_realloc(allocator, buf, new_cap);
            if (grown == WY_NULL) { wy_allocator_free(allocator, buf); return native_fault_(context, "read: out of memory"); }
            buf = grown;
            cap = new_cap;
        }
        ssize_t n = read((int) handle, buf + total, cap - total);
        if (n < 0) {
            int err_no = errno;
            wy_allocator_free(allocator, buf);
            wy_value result;
            if (os_error_value_(context, err_no, &result) != WY_ERR_NONE) { return native_fault_(context, "read: out of memory building OSError"); }
            wy_context_set_result(context, 0, result);
            return WY_EXEC_DONE;
        }
        if (n == 0) { break; }
        total += (wy_uword) n;
        if (!read_all && total >= want) { break; }
    }

    wy_string* result_str = WY_NULL;
    wy_error err = wy_string_new(context, buf, total, &result_str);
    wy_allocator_free(allocator, buf);
    if (err != WY_ERR_NONE) { return native_fault_(context, "read: out of memory building result"); }
    wy_context_set_result(context, 0, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) result_str));
    return WY_EXEC_DONE;
}

static wy_exec_state io_write_exec_(wy_context* context, wy_primitive c_data)
{
    WY_UNUSED(c_data);
    wy_word handle;
    wy_string* data = WY_NULL;
    if (!arg_word_(context, 0, &handle) || !arg_str_(context, 1, &data)) {
        return native_fault_(context, "write: handle must be an integer, data a string");
    }
    wy_uword total = 0;
    while (total < data->len) {
        ssize_t n = write((int) handle, data->str + total, data->len - total);
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

static const io_exec_entry_ io_exec_natives_[] = {
    { "open",  2, 2, io_open_exec_ },
    { "read",  2, 2, io_read_exec_ },
    { "write", 2, 2, io_write_exec_ },
    { "lseek", 3, 3, io_lseek_exec_ },
    { "dup2",  2, 2, io_dup2_exec_ },
    { "close", 1, 1, io_close_exec_ },
    { "flush", 1, 1, io_flush_exec_ },
};

typedef struct io_leaf_entry_
{
    const char* name;
    wy_u8 min_argc;
    wy_u8 max_argc;
    wy_native_leaf_fn fn;
} io_leaf_entry_;

/*
 * `println` under `std::io` (`std::io::println`): some samples import
 * `std::io` and call `std::io::println` directly rather than the bare
 * builtin (e.g. eval_closures.wy, eval_modules.wy) - the reference's
 * corelib/std/io.wy wrapper exposes it there too. Reuses the exact same
 * rendering as the bare `println` (src/builtin/builtins.c) rather than a
 * second implementation or an embedded `io.wy` wrapper, per epic 5/M4's
 * "seven natives exported directly under std::io" scope call.
 */
static const io_leaf_entry_ io_leaf_natives_[] = {
    { "println", 0, 255, wy_builtin_println_body_f },
};

enum { WY_IO_EXEC_COUNT = sizeof(io_exec_natives_) / sizeof(io_exec_natives_[0]),
    WY_IO_LEAF_COUNT = sizeof(io_leaf_natives_) / sizeof(io_leaf_natives_[0]),
    WY_IO_NATIVE_COUNT = WY_IO_EXEC_COUNT + WY_IO_LEAF_COUNT, WY_IO_CONST_COUNT = 3,
    WY_IO_GLOBAL_COUNT = WY_IO_NATIVE_COUNT + WY_IO_CONST_COUNT };

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

    for (wy_uword i = 0; i < WY_IO_LEAF_COUNT; i++) {
        const io_leaf_entry_* entry = &io_leaf_natives_[i];
        wy_symbol sym;
        err = wy_context_intern(context, entry->name, wy_strlen_f(entry->name), &sym);
        if (err != WY_ERR_NONE) { return err; }
        wy_native* native = WY_NULL;
        err = wy_native_leaf_new(context, sym, entry->min_argc, entry->max_argc, entry->fn, &native);
        if (err != WY_ERR_NONE) { return err; }
        module->globals[slot] = wy_value_object(WY_TYPE_TAG_NATIVE, (wy_object*) native);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    static const struct { const char* name; wy_word value; } consts[WY_IO_CONST_COUNT] = {
        { "STDIN", 0 }, { "STDOUT", 1 }, { "STDERR", 2 },
    };
    for (wy_uword i = 0; i < WY_IO_CONST_COUNT; i++) {
        wy_symbol sym;
        err = wy_context_intern(context, consts[i].name, wy_strlen_f(consts[i].name), &sym);
        if (err != WY_ERR_NONE) { return err; }
        module->globals[slot] = wy_value_word(consts[i].value);
        err = wy_slot_dict_add_entry(&module->exports, sym, slot);
        if (err != WY_ERR_NONE) { return err; }
        slot++;
    }

    WY_ASSERT(slot == WY_IO_GLOBAL_COUNT);
    *out = module;
    return WY_ERR_NONE;
}

/**
 * wypoc's compiler_bc emits an implicit `import std` ahead of `import
 * std::io` for any dotted import path (the parent package, same as
 * Python's `import a.b` also binding bare `a`) - see the "std" static
 * string and its own `import`/`gset` pair ahead of "std::io"'s in a
 * compiled script's init code. There is no real content behind that
 * parent: it exists only so the ancestor import resolves. Register an
 * empty BUILTIN module under "std" so wy_link_import's already-registered
 * check finds it and never reaches the (real) filesystem hook for a
 * package that has no `std.wyc` of its own.
 */
static wy_error install_std_package_(wy_context* context)
{
    wy_module* module = wy_module_new_f(context);
    if (module == WY_NULL) { return WY_ERR_NOMEM; }
    module->state = WY_MODULE_BUILTIN;

    wy_string* path = WY_NULL;
    wy_error err = wy_string_strdup(context, "std", &path);
    if (err != WY_ERR_NONE) { return err; }
    module->import_path = path;
    err = wy_context_intern(context, "std", 3, &module->name);
    if (err != WY_ERR_NONE) { return err; }

    return wy_context_module_register(context, module, WY_NULL);
}

wy_error wy_io_module_install(wy_context* context)
{
    if (context == WY_NULL) { return WY_ERR_INVAL; }

    wy_error err = install_std_package_(context);
    if (err != WY_ERR_NONE) { return err; }

    wy_module* module = WY_NULL;
    err = wy_io_module_new(context, &module);
    if (err != WY_ERR_NONE) { return err; }

    wy_string* path = WY_NULL;
    err = wy_string_strdup(context, "std::io", &path);
    if (err != WY_ERR_NONE) { return err; }
    module->import_path = path;
    err = wy_context_intern(context, "std::io", wy_strlen_f("std::io"), &module->name);
    if (err != WY_ERR_NONE) { return err; }

    return wy_context_module_register(context, module, WY_NULL);
}
