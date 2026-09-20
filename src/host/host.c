#define _POSIX_C_SOURCE 200809L

#include <wyrm/host.h>

#include "host_internal.h"

#include "../embed/builtins.h"
#include "../embed/std/io_native.h"

#include <wyrm/builtins.h>
#include <wyrm/bytes.h>
#include <wyrm/error.h>
#include <wyrm/fiber.h>
#include <wyrm/image_loader.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/platform/hosted/expand_native.h>
#include <wyrm/session.h>
#include <wyrm/string.h>
#include <wyrm/sys/string.h>
#include <wyrm/vm.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { WY_HOST_STACK_LEN = 1u << 16, WY_HOST_FRAME_COUNT = 4096, WY_HOST_MAX_COMPILE_DEPTH = 8 };

/* ------------------------------------------------------------------------
 * Compiling a source to a container on a scratch machine (moved from main.c so
 * the executable and every embedder share it).
 * ------------------------------------------------------------------------ */

/**
 * Epic 11 M2/M3: run the embedded compiler's `compile_source` on
 * `compile_ctx` - the builtin table's wyrm::tools::compile_source, never a
 * filesystem lookup (that path would need the compiler to compile itself) -
 * and answer the fresh container bytes, allocated on `compile_ctx`. On
 * failure `*msg` names it for the CLI's error line (it points into the
 * context, so print before destroying anything).
 */
wy_error wy_host_compile_source_bytes_(wy_context* compile_ctx,
    const char* path, const wy_u8* source, wy_uword source_len,
    wy_u8** out_bytes, wy_uword* out_len, const char** msg)
{
    static const char host_name[] = "wyrm::tools::compile_source";
    wy_error err = WY_ERR_NONE;

    wy_string* host_path = WY_NULL;
    err = wy_string_new(compile_ctx, host_name, sizeof(host_name) - 1, &host_path);
    if (err != WY_ERR_NONE) { return err; }
    wy_module* host = WY_NULL;
    err = wy_link_import(compile_ctx, host_path, &host);
    if (err != WY_ERR_NONE) { *msg = "cannot load the embedded compiler"; return err; }
    if (host->state == WY_MODULE_LOADED) {
        err = wy_module_run_init(compile_ctx, host);
        if (err != WY_ERR_NONE) {
            wy_value fault = compile_ctx->current_fiber->fault;
            *msg = (wy_value_is_error(fault) && fault.data.gc_object != WY_NULL &&
                    ((wy_error_obj*) fault.data.gc_object)->what != WY_NULL)
                ? ((wy_error_obj*) fault.data.gc_object)->what->str
                : "the embedded compiler failed to initialise";
            return err;
        }
    }

    wy_symbol fn_sym = WY_NULL;
    err = wy_context_intern(compile_ctx, "compile_source", sizeof("compile_source") - 1, &fn_sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_value* fn_slot = WY_NULL;
    err = wy_link_scope_member(wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) host), fn_sym, &fn_slot);
    if (err != WY_ERR_NONE || fn_slot == WY_NULL) { *msg = "the embedded compiler has no compile_source"; return WY_ERR_UNBOUND; }

    /* The module name is the entry's basename without .wy (compiler_main's
     * naming); the module still runs as __main__. */
    const char* base = strrchr(path, '/');
    base = (base != WY_NULL) ? base + 1 : path;
    size_t base_len = strlen(base);
    if (base_len > 3 && memcmp(base + base_len - 3, ".wy", 3) == 0) { base_len -= 3; }

    wy_string* src_str = WY_NULL;
    wy_string* name_str = WY_NULL;
    wy_string* path_str = WY_NULL;
    err = wy_string_new(compile_ctx, (const char*) source, source_len, &src_str);
    if (err == WY_ERR_NONE) { err = wy_string_new(compile_ctx, base, base_len, &name_str); }
    if (err == WY_ERR_NONE) { err = wy_string_new(compile_ctx, path, strlen(path), &path_str); }
    if (err != WY_ERR_NONE) { return err; }

    wy_value args[3] = {
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) src_str),
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) name_str),
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) path_str),
    };
    wy_value result = wy_value_nil();
    if (wy_context_root_push_f(compile_ctx, &result) != WY_ERR_NONE) { return WY_ERR_NOMEM; }

    err = wy_vm_call_sync(compile_ctx, *fn_slot, args, 3, &result, 1);
    if (err != WY_ERR_NONE) {
        wy_value fault = compile_ctx->current_fiber->fault;
        *msg = (wy_value_is_error(fault) && fault.data.gc_object != WY_NULL &&
                ((wy_error_obj*) fault.data.gc_object)->what != WY_NULL)
            ? ((wy_error_obj*) fault.data.gc_object)->what->str : "compile fault";
        wy_context_root_pop_f(compile_ctx);
        return err;
    }
    if (wy_value_is_error(result)) {
        wy_error_obj* failure = (wy_error_obj*) result.data.gc_object;
        *msg = (failure != WY_NULL && failure->what != WY_NULL) ? failure->what->str : "compile failed";
        wy_context_root_pop_f(compile_ctx);
        return WY_ERR_FAULT;
    }
    if (result.type != WY_TYPE_TAG_BYTES) {
        *msg = "the embedded compiler returned no image";
        wy_context_root_pop_f(compile_ctx);
        return WY_ERR_IMAGE;
    }

    /* Copy the container out of the wy_bytes value (which stops being
     * rooted below). */
    wy_bytes* blob = (wy_bytes*) result.data.gc_object;
    *out_bytes = (wy_u8*) wy_context_gc_alloc(compile_ctx, blob->len);
    if (*out_bytes == WY_NULL) { wy_context_root_pop_f(compile_ctx); return WY_ERR_NOMEM; }
    wy_memcpy(*out_bytes, blob->data, blob->len);
    *out_len = blob->len;
    wy_context_root_pop_f(compile_ctx);
    return WY_ERR_NONE;
}

static wy_uword compile_scratch_depth_ = 0;

/**
 * The import hook's compile fn (epic 11 M3): the requesting VM is
 * suspended mid-import, so the compiler runs on an independent scratch
 * machine - the no-recursion rule forbids calling back into the
 * requester's execution (the same shape as std::expand's throwaway VM).
 * The scratch context shares the search path (builtin table, roots,
 * cache config), so its own imports resolve exactly like the CLI's.
 * The depth guard bounds .wy import cycles at compile time (a imports b
 * imports a would otherwise nest machines forever); each level's VM loop
 * nests on the C stack, so the cap is also a stack guard.
 */
wy_error wy_host_compile_on_scratch_(void* ud, wy_context* requester, const char* source_path,
    const wy_u8* source, wy_uword source_len, wy_u8** out_bytes, wy_uword* out_len, const char** msg)
{
    if (compile_scratch_depth_ >= WY_HOST_MAX_COMPILE_DEPTH) {
        *msg = "import cycle while compiling .wy sources";
        return WY_ERR_CYCLE;
    }
    compile_scratch_depth_ += 1;
    wy_machine* machine = wy_cmachine_new();
    if (machine == WY_NULL) { *msg = "out of memory creating the compile machine"; return WY_ERR_NOMEM; }
    wy_error err = WY_ERR_NONE;
    wy_context* ctx = WY_NULL;
    wy_u8* blob = WY_NULL;
    wy_uword blob_len = 0;

    ctx = wy_cmachine_context_new(machine);
    if (ctx == WY_NULL) { *msg = "out of memory creating the compile context"; err = WY_ERR_NOMEM; goto done; }
    wy_fiber* fiber = wy_fiber_create(ctx, WY_HOST_STACK_LEN, WY_HOST_FRAME_COUNT);
    if (fiber == WY_NULL || wy_context_attach_fiber(ctx, fiber) != WY_ERR_NONE) {
        *msg = "out of memory creating the compile fiber"; err = WY_ERR_NOMEM; goto done;
    }
    ctx->import_hook = wy_import_fs_hook;
    ctx->import_ud = ud;  /* the search path (builtin table, roots, cache config) */

    wy_module* builtins = WY_NULL;
    err = wy_builtins_new(ctx, &builtins);
    if (err == WY_ERR_NONE) {
        ctx->builtins = builtins;
        /* std package + std::expand: compile_source imports the decorator
         * expander (wyrm::compiler::expansion), whose imports need both.
         * The std::io natives too: build_bc writes its artifacts through the
         * embedded std::io, which is written over them. This is
         * the compile worker, not the security boundary - an expansion
         * child spawned for decorators installs its own minimal module
         * set and stays io-free (D10). */
        err = wy_io_natives_install(ctx);
        if (err == WY_ERR_NONE) { err = wy_expand_module_install(ctx); }
    }
    if (err != WY_ERR_NONE) { *msg = "cannot initialise the compile machine"; goto done; }

    err = wy_host_compile_source_bytes_(ctx, source_path, source, source_len, &blob, &blob_len, msg);
    if (err != WY_ERR_NONE) { goto done; }

    /* Copy out of the scratch heap (destroyed below) into the requester. */
    *out_bytes = (wy_u8*) wy_context_gc_alloc(requester, blob_len);
    if (*out_bytes == WY_NULL) { *msg = "out of memory"; err = WY_ERR_NOMEM; goto done; }
    wy_memcpy(*out_bytes, blob, blob_len);
    *out_len = blob_len;

done:
    compile_scratch_depth_ -= 1;
    if (ctx != WY_NULL) { wy_cmachine_context_destroy(ctx); }
    if (machine != WY_NULL) { (void) wy_cmachine_destroy_residual(machine); }
    return err;
}


/* ------------------------------------------------------------------------
 * The host object
 * ------------------------------------------------------------------------ */

static void default_write_(wy_context* context, const char* bytes, wy_uword len, void* ud)
{
    (void) context; (void) ud;
    fwrite(bytes, 1, len, stdout);
}

static void set_error_(wy_host* host, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(host->error, sizeof(host->error), fmt, args);
    va_end(args);
}

static const char* fault_text_(wy_context* context)
{
    wy_value fault = context->current_fiber->fault;
    if (wy_value_is_error(fault) && fault.data.gc_object != WY_NULL) {
        wy_error_obj* e = (wy_error_obj*) fault.data.gc_object;
        if (e->what != WY_NULL) { return e->what->str; }
    }
    return "fault";
}

void wy_host_config_default(wy_host_config* config)
{
    memset(config, 0, sizeof(*config));
}

const char* wy_host_error(const wy_host* host)
{
    return host != WY_NULL ? host->error : "";
}

wy_context* wy_host_context(wy_host* host)
{
    return host != WY_NULL ? host->context : WY_NULL;
}

static bool export_fn_(wy_host* host, const char* name, wy_value* out)
{
    wy_symbol sym = WY_NULL;
    if (wy_context_intern(host->context, name, strlen(name), &sym) != WY_ERR_NONE) { return false; }
    wy_uword slot = wy_slot_dict_get(&host->compiler->exports, sym);
    if (slot == WY_SLOT_INVALID) { return false; }
    *out = host->compiler->globals[slot];
    return out->type == WY_TYPE_TAG_FUNCTION;
}

/* Call a compiler entry point; a fault inside it is an internal error. */
static wy_error call_(wy_host* host, wy_value fn, const wy_value* args, wy_uword argc, wy_value* out)
{
    wy_error err = wy_vm_call_sync(host->context, fn, args, argc, out, 1);
    if (err != WY_ERR_NONE) {
        set_error_(host, "internal error in the session compiler: %s", fault_text_(host->context));
        host->context->current_fiber->fault = wy_value_nil();
        return err;
    }
    return WY_ERR_NONE;
}

static wy_error string_value_(wy_host* host, const char* text, size_t len, wy_value* out)
{
    wy_string* s = WY_NULL;
    wy_error err = wy_string_new(host->context, text, (wy_uword) len, &s);
    if (err != WY_ERR_NONE) { return err; }
    *out = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) s);
    return WY_ERR_NONE;
}

wy_error wy_host_reset_session_(wy_host* host)
{
    if (host->session_module != WY_NULL) {
        (void) wy_context_module_unregister(host->context, host->session_module);
        host->session_module = WY_NULL;
    }
    wy_error err = wy_module_session_new(host->context, &host->session_config, &host->session_module);
    if (err != WY_ERR_NONE) { set_error_(host, "cannot reserve the session"); return err; }
    return call_(host, host->fn_new, WY_NULL, 0, &host->session);
}

wy_error wy_host_new(const wy_host_config* config_in, wy_host** out)
{
    if (out == WY_NULL) { return WY_ERR_INVAL; }
    *out = WY_NULL;
    wy_host_config config;
    if (config_in != WY_NULL) { config = *config_in; } else { wy_host_config_default(&config); }

    wy_host* host = (wy_host*) calloc(1, sizeof(*host));
    if (host == WY_NULL) { return WY_ERR_NOMEM; }
    host->session = wy_value_nil();
    host->result = wy_value_nil();
    host->output = config.output != WY_NULL ? config.output : default_write_;
    host->output_ud = config.output_ud;

    host->machine = wy_cmachine_new();
    host->context = host->machine != WY_NULL ? wy_cmachine_context_new(host->machine) : WY_NULL;
    if (host->context == WY_NULL) { wy_host_free(host); return WY_ERR_NOMEM; }
    wy_context* context = host->context;

    wy_fiber* fiber = wy_fiber_create(context, WY_HOST_STACK_LEN, WY_HOST_FRAME_COUNT);
    if (fiber == WY_NULL || wy_context_attach_fiber(context, fiber) != WY_ERR_NONE) {
        wy_host_free(host);
        return WY_ERR_NOMEM;
    }
    context->io.write = host->output;
    context->io.ud = host->output_ud;

    wy_import_fs_search_path_init_s(&host->search);
    wy_allocator* allocator = wy_context_get_machine(context)->allocator;
    wy_error err = WY_ERR_NONE;
    for (wy_uword i = 0; err == WY_ERR_NONE && i < config.include_count; i++) {
        err = wy_import_fs_add_root(allocator, &host->search, config.include_paths[i]);
    }
    host->search.builtins = wyrm_builtin_modules;
    host->search.builtin_count = wyrm_builtin_module_count;
    host->search.verbose = config.verbose;
    if (err == WY_ERR_NONE && config.cache_dir != WY_NULL) {
        /* The cache prefix mapping needs an absolute path: resolve a relative one now. */
        if (config.cache_dir[0] == '/') {
            host->cache_dir = strdup(config.cache_dir);
        } else {
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd)) != WY_NULL) {
                size_t n = strlen(cwd) + 1 + strlen(config.cache_dir) + 1;
                host->cache_dir = (char*) malloc(n);
                if (host->cache_dir != WY_NULL) { snprintf(host->cache_dir, n, "%s/%s", cwd, config.cache_dir); }
            }
        }
        if (host->cache_dir == WY_NULL) { err = WY_ERR_NOMEM; }
        host->search.cache_dir = host->cache_dir;
    }
    host->search.compile = wy_host_compile_on_scratch_;
    host->search.compile_ud = &host->search;
    context->import_hook = wy_import_fs_hook;
    context->import_ud = &host->search;

    wy_module* builtins = WY_NULL;
    if (err == WY_ERR_NONE) { err = wy_builtins_new(context, &builtins); }
    if (err == WY_ERR_NONE) {
        context->builtins = builtins;
        err = wy_io_natives_install(context);
    }
    if (err == WY_ERR_NONE) { err = wy_expand_module_install(context); }

    wy_string* path = WY_NULL;
    if (err == WY_ERR_NONE) { err = wy_string_strdup(context, "wyrm::tools::compile_source", &path); }
    if (err == WY_ERR_NONE) { err = wy_link_import(context, path, &host->compiler); }
    if (err == WY_ERR_NONE) { err = wy_module_run_init(context, host->compiler); }
    if (err == WY_ERR_NONE
        && (!export_fn_(host, "session_new", &host->fn_new) || !export_fn_(host, "session_compile", &host->fn_compile)
            || !export_fn_(host, "session_incomplete", &host->fn_incomplete) || !export_fn_(host, "session_undo", &host->fn_undo))) {
        err = WY_ERR_UNBOUND;
    }
    if (err == WY_ERR_NONE) { err = wy_context_root_push_f(context, &host->session); }
    if (err == WY_ERR_NONE) { err = wy_context_root_push_f(context, &host->result); }

    wy_session_config_default(&host->session_config);
    if (config.code_words != 0) { host->session_config.capacity[WY_SESSION_CODE] = config.code_words; }
    if (err == WY_ERR_NONE) { err = wy_host_reset_session_(host); }

    if (err != WY_ERR_NONE) { wy_host_free(host); return err; }
    *out = host;
    return WY_ERR_NONE;
}

void wy_host_free(wy_host* host)
{
    if (host == WY_NULL) { return; }
    if (host->context != WY_NULL) {
        wy_allocator* allocator = wy_context_get_machine(host->context)->allocator;
        wy_import_fs_search_path_finalize_f(allocator, &host->search);
        wy_cmachine_context_destroy(host->context);
    }
    if (host->machine != WY_NULL) { (void) wy_cmachine_destroy_residual(host->machine); }
    for (size_t i = 0; i < host->dir_count; i++) { free(host->dirs[i]); }
    free(host->dirs);
    free(host->cache_dir);
    free(host);
}

/* ------------------------------------------------------------------------
 * Running code
 * ------------------------------------------------------------------------ */

wy_error wy_host_eval_(wy_host* host, const char* source, size_t len)
{
    host->error[0] = '\0';
    host->result = wy_value_nil();

    wy_value args[2];
    args[0] = host->session;
    wy_error err = string_value_(host, source, len, &args[1]);
    if (err != WY_ERR_NONE) { set_error_(host, "out of memory"); return err; }

    wy_value blob = wy_value_nil();
    err = call_(host, host->fn_compile, args, 2, &blob);
    if (err != WY_ERR_NONE) { return WY_ERR_UNKNOWN; }
    host->result = blob;   /* rooted while the loader works on it */

    if (blob.type == WY_TYPE_TAG_ERROR) {
        wy_error_obj* e = (wy_error_obj*) blob.data.gc_object;
        set_error_(host, "%s", (e != WY_NULL && e->what != WY_NULL) ? e->what->str : "error");
        host->result = wy_value_nil();
        return WY_ERR_IMAGE;
    }
    if (blob.type != WY_TYPE_TAG_BYTES) {
        set_error_(host, "internal error: the compiler answered no image");
        host->result = wy_value_nil();
        return WY_ERR_UNKNOWN;
    }

    wy_bytes* bytes = (wy_bytes*) blob.data.gc_object;
    wy_module_image image;
    err = wy_image_from_bytes(bytes->data, bytes->len, &image);
    wy_uword init = 0;
    if (err == WY_ERR_NONE) { err = wy_module_extend(host->context, host->session_module, &image, &init); }
    if (err != WY_ERR_NONE) {
        if (err == WY_ERR_SESSION_FULL) {
            set_error_(host, "session limit reached");
        } else {
            set_error_(host, "internal error: cannot load the input (%d)", (int) err);
        }
        /* The compiler already counted this input: take it back so both sides stay in step. */
        wy_value undone = wy_value_nil();
        (void) call_(host, host->fn_undo, &host->session, 1, &undone);
        host->result = wy_value_nil();
        return err;
    }

    wy_value value = wy_value_nil();
    host->result = wy_value_nil();
    err = wy_module_run_function(host->context, host->session_module, init, &value);
    if (err != WY_ERR_NONE) {
        set_error_(host, "%s", fault_text_(host->context));
        host->context->current_fiber->fault = wy_value_nil();
        return WY_ERR_FAULT;
    }
    host->result = value;
    return WY_ERR_NONE;
}

wy_error wy_host_eval(wy_host* host, const char* source, wy_value* result)
{
    if (host == WY_NULL || source == WY_NULL) { return WY_ERR_INVAL; }
    wy_error err = wy_host_eval_(host, source, strlen(source));
    if (err == WY_ERR_NONE && result != WY_NULL) { *result = host->result; }
    return err;
}

static bool add_dir_(wy_host* host, const char* dir)
{
    for (size_t i = 0; i < host->dir_count; i++) {
        if (strcmp(host->dirs[i], dir) == 0) { return true; }
    }
    char** grown = (char**) realloc(host->dirs, (host->dir_count + 1) * sizeof(char*));
    if (grown == WY_NULL) { return false; }
    host->dirs = grown;
    host->dirs[host->dir_count] = strdup(dir);
    if (host->dirs[host->dir_count] == WY_NULL) { return false; }
    host->dir_count++;
    wy_allocator* allocator = wy_context_get_machine(host->context)->allocator;
    return wy_import_fs_add_root(allocator, &host->search, dir) == WY_ERR_NONE;
}

wy_error wy_host_load_file(wy_host* host, const char* path)
{
    if (host == WY_NULL || path == WY_NULL) { return WY_ERR_INVAL; }
    host->error[0] = '\0';
    FILE* file = fopen(path, "rb");
    if (file == WY_NULL) { set_error_(host, "%s: cannot open the file", path); return WY_ERR_UNBOUND; }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = size >= 0 ? (char*) malloc((size_t) size + 1) : WY_NULL;
    if (text == WY_NULL || fread(text, 1, (size_t) size, file) != (size_t) size) {
        fclose(file);
        free(text);
        set_error_(host, "%s: cannot read the file", path);
        return WY_ERR_UNBOUND;
    }
    fclose(file);
    text[size] = '\0';

    /* Sibling imports: the script's own directory is an import root. */
    const char* slash = strrchr(path, '/');
    char* dir = slash != WY_NULL ? strndup(path, (size_t) (slash - path == 0 ? 1 : slash - path)) : strdup(".");
    if (dir != WY_NULL) { (void) add_dir_(host, dir); free(dir); }

    wy_error err = wy_host_eval_(host, text, (size_t) size);
    free(text);
    if (err != WY_ERR_NONE) {
        char detail[sizeof(host->error)];
        memcpy(detail, host->error, sizeof(detail));
        set_error_(host, "%s: %.400s", path, detail);
    }
    return err;
}

bool wy_host_needs_more(wy_host* host, const char* source)
{
    if (host == WY_NULL || source == WY_NULL) { return false; }
    wy_value arg, verdict = wy_value_nil();
    if (string_value_(host, source, strlen(source), &arg) != WY_ERR_NONE) { return false; }
    if (call_(host, host->fn_incomplete, &arg, 1, &verdict) != WY_ERR_NONE) { return false; }
    return verdict.type == WY_TYPE_TAG_BOOL && verdict.data.flag;
}

/* ------------------------------------------------------------------------
 * Variables
 * ------------------------------------------------------------------------ */

static bool valid_name_(const char* name)
{
    if (name == WY_NULL || !(isalpha((unsigned char) name[0]) || name[0] == '_')) { return false; }
    for (const char* p = name; *p; p++) {
        if (!(isalnum((unsigned char) *p) || *p == '_')) { return false; }
    }
    return true;
}

static wy_uword slot_of_(wy_host* host, const char* name)
{
    wy_symbol sym = WY_NULL;
    if (wy_context_intern(host->context, name, strlen(name), &sym) != WY_ERR_NONE) { return WY_SLOT_INVALID; }
    return wy_slot_dict_get(&host->session_module->exports, sym);
}

bool wy_host_has(wy_host* host, const char* name)
{
    if (host == WY_NULL || !valid_name_(name)) { return false; }
    wy_uword slot = slot_of_(host, name);
    return slot != WY_SLOT_INVALID && !wy_value_is_unset(host->session_module->globals[slot]);
}

wy_error wy_host_get(wy_host* host, const char* name, wy_value* out)
{
    if (host == WY_NULL || out == WY_NULL || !valid_name_(name)) { return WY_ERR_INVAL; }
    wy_uword slot = slot_of_(host, name);
    if (slot == WY_SLOT_INVALID || wy_value_is_unset(host->session_module->globals[slot])) {
        set_error_(host, "no variable named '%s'", name);
        return WY_ERR_UNBOUND;
    }
    *out = host->session_module->globals[slot];
    return WY_ERR_NONE;
}

wy_error wy_host_set(wy_host* host, const char* name, wy_value value)
{
    if (host == WY_NULL || !valid_name_(name)) { return WY_ERR_INVAL; }
    /* Root the value across the declaring eval below (which may collect). */
    wy_value held = value;
    wy_error err = wy_context_root_push_f(host->context, &held);
    if (err != WY_ERR_NONE) { return err; }

    wy_uword slot = slot_of_(host, name);
    if (slot == WY_SLOT_INVALID) {
        /* Declare it: an ordinary `name := nil` input, so it is a real binding the
         * compiler knows (and that earlier forward references are filled by). */
        char declaration[256];
        int n = snprintf(declaration, sizeof(declaration), "%s := nil", name);
        if (n < 0 || (size_t) n >= sizeof(declaration)) { wy_context_root_pop_f(host->context); return WY_ERR_INVAL; }
        err = wy_host_eval_(host, declaration, (size_t) n);
        if (err == WY_ERR_NONE) { slot = slot_of_(host, name); }
    }
    if (err == WY_ERR_NONE && slot == WY_SLOT_INVALID) { err = WY_ERR_UNBOUND; }
    if (err == WY_ERR_NONE) { host->session_module->globals[slot] = held; }
    wy_context_root_pop_f(host->context);
    return err;
}

wy_error wy_host_set_nil(wy_host* host, const char* name) { return wy_host_set(host, name, wy_value_nil()); }
wy_error wy_host_set_bool(wy_host* host, const char* name, bool value) { return wy_host_set(host, name, wy_value_bool(value)); }
wy_error wy_host_set_int(wy_host* host, const char* name, long value) { return wy_host_set(host, name, wy_value_word((wy_word) value)); }
wy_error wy_host_set_float(wy_host* host, const char* name, double value) { return wy_host_set(host, name, wy_value_float((wy_float) value)); }

wy_error wy_host_set_string(wy_host* host, const char* name, const char* value)
{
    if (host == WY_NULL || value == WY_NULL) { return WY_ERR_INVAL; }
    wy_value text;
    wy_error err = string_value_(host, value, strlen(value), &text);
    if (err != WY_ERR_NONE) { return err; }
    return wy_host_set(host, name, text);
}

wy_error wy_host_get_bool(wy_host* host, const char* name, bool* out)
{
    wy_value v;
    wy_error err = wy_host_get(host, name, &v);
    if (err != WY_ERR_NONE) { return err; }
    if (v.type != WY_TYPE_TAG_BOOL) { set_error_(host, "'%s' is not a bool", name); return WY_ERR_BAD_TYPE; }
    *out = v.data.flag;
    return WY_ERR_NONE;
}

wy_error wy_host_get_int(wy_host* host, const char* name, long* out)
{
    wy_value v;
    wy_error err = wy_host_get(host, name, &v);
    if (err != WY_ERR_NONE) { return err; }
    if (v.type != WY_TYPE_TAG_WORD) { set_error_(host, "'%s' is not an integer", name); return WY_ERR_BAD_TYPE; }
    *out = (long) v.data.word;
    return WY_ERR_NONE;
}

wy_error wy_host_get_float(wy_host* host, const char* name, double* out)
{
    wy_value v;
    wy_error err = wy_host_get(host, name, &v);
    if (err != WY_ERR_NONE) { return err; }
    if (v.type == WY_TYPE_TAG_FLOAT) { *out = (double) v.data.fp; return WY_ERR_NONE; }
    if (v.type == WY_TYPE_TAG_WORD) { *out = (double) v.data.word; return WY_ERR_NONE; }
    set_error_(host, "'%s' is not a number", name);
    return WY_ERR_BAD_TYPE;
}

wy_error wy_host_get_string(wy_host* host, const char* name, char* buf, size_t cap, size_t* out_len)
{
    wy_value v;
    wy_error err = wy_host_get(host, name, &v);
    if (err != WY_ERR_NONE) { return err; }
    if (v.type != WY_TYPE_TAG_STR) { set_error_(host, "'%s' is not a string", name); return WY_ERR_BAD_TYPE; }
    wy_string* s = (wy_string*) v.data.gc_object;
    if (out_len != WY_NULL) { *out_len = s->len; }
    if (buf == WY_NULL || cap == 0) { return WY_ERR_RANGE; }
    size_t n = s->len < cap - 1 ? s->len : cap - 1;
    memcpy(buf, s->str, n);
    buf[n] = '\0';
    return n < s->len ? WY_ERR_RANGE : WY_ERR_NONE;
}

typedef struct capture_
{
    char* buf;
    size_t cap;
    size_t len;   /* full length, even past cap */
} capture_;

static void capture_write_(wy_context* context, const char* bytes, wy_uword len, void* ud)
{
    (void) context;
    capture_* c = (capture_*) ud;
    if (c->cap > 0 && c->len < c->cap - 1) {
        size_t room = c->cap - 1 - c->len;
        memcpy(c->buf + c->len, bytes, len < room ? len : room);
    }
    c->len += len;
}

wy_error wy_host_format(wy_host* host, wy_value value, char* buf, size_t cap, size_t* out_len)
{
    if (host == WY_NULL || (buf == WY_NULL && cap > 0)) { return WY_ERR_INVAL; }
    capture_ c = { buf, cap, 0 };
    wy_io_write_fn saved = host->context->io.write;
    void* saved_ud = host->context->io.ud;
    host->context->io.write = capture_write_;
    host->context->io.ud = &c;
    wy_value ignored[1] = { wy_value_nil() };
    wy_error err = wy_builtin_println_body_f(host->context, &value, 1, ignored, 0);
    host->context->io.write = saved;
    host->context->io.ud = saved_ud;
    if (err != WY_ERR_NONE) { return err; }
    if (c.len > 0) { c.len--; }   /* println's trailing newline */
    if (out_len != WY_NULL) { *out_len = c.len; }
    if (cap > 0) { buf[c.len < cap - 1 ? c.len : cap - 1] = '\0'; }
    return (cap == 0 || c.len >= cap) ? WY_ERR_RANGE : WY_ERR_NONE;
}
