#include <wyrm.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wyrm/builtins.h>
#include <wyrm/bytes.h>
#include <wyrm/error.h>
#include <wyrm/fiber.h>
#include <wyrm/image.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/opcode_names.h>
#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/platform/hosted/import_cache.h>
#include <wyrm/platform/hosted/import_fs.h>
#include <wyrm/platform/hosted/expand_native.h>
#include "../embed/std/io_native.h"
#include "repl.h"
#include <wyrm/slot.h>
#include <wyrm/string.h>
#include <wyrm/vm.h>

#include "../embed/builtins.h"

enum { WY_MAIN_STACK_LEN = 1u << 16, WY_MAIN_FRAME_COUNT = 4096, WY_MAIN_MAX_INCLUDE = 64 };

static void io_write_stdout_(wy_context* context, const char* bytes, wy_uword len, void* ud)
{
    WY_UNUSED(context); WY_UNUSED(ud);
    fwrite(bytes, 1, len, stdout);
}

/**
 * `wyrm file.wyc --sections`: the one-line-per-invocation summary the M1
 * exit criterion checks. Sections a module tracks no count for
 * (slot_defaults) or that a VM must ignore entirely (debug, §8.9) are
 * never printed; every other optional section is printed only if it has
 * at least one entry - an absent section and an empty one print alike.
 */
static void print_sections(const wy_module* module)
{
    printf("header n=%s v=1 g=%lu l=%u",
        module->name != WY_NULL ? module->name : "",
        (unsigned long) module->global_count,
        module->init_nlocals);

    if (module->static_count > 0) { printf(" | statics %lu", (unsigned long) module->static_count); }
    if (module->symbol_count > 0) { printf(" | symbols %lu", (unsigned long) module->symbol_count); }
    if (module->function_count > 0) { printf(" | functions %lu", (unsigned long) module->function_count); }
    if (module->class_count > 0) { printf(" | classes %lu", (unsigned long) module->class_count); }
    if (module->message_count > 0) { printf(" | messages %lu", (unsigned long) module->message_count); }
    printf(" | code %lu words", (unsigned long) module->code_len);
    if (module->exports.entry_count > 0) { printf(" | exports %lu", (unsigned long) module->exports.entry_count); }
    if (module->free_names.entry_count > 0) { printf(" | free %lu", (unsigned long) module->free_names.entry_count); }
    printf("\n");
}

/**
 * `wyrm file.wyc --disasm`: one line per instruction, decoded through
 * opcode.h's own accessor macros and wy_opcode_names - the same table the
 * compiler's `.wy_a` listing (opcodes.py) reads, so scripts/check_disasm.sh
 * can diff mnemonics between the two independent decoders.
 *
 * No symbol/static/register-name resolution: that needs the interpreter's
 * value model (epic 2). Operands print as the raw fields opcode.h defines.
 */
static void print_disasm(const wy_module* module)
{
    wy_uword idx = 0;
    while (idx < module->code_len) {
        const wy_u32* cur = &module->code[idx];
        wy_u8 op = WYRM_OP(cur);
        wy_uword words = WYRM_OP_WORDS(op);
        const char* mnemonic = wy_opcode_names[op] != NULL ? wy_opcode_names[op] : "?";

        if (idx + words > module->code_len) {
            printf("%04lu  %-12s (truncated: needs %lu words, %lu remain)\n",
                (unsigned long) idx, mnemonic, (unsigned long) words, (unsigned long) (module->code_len - idx));
            break;
        }

        if (words == 1) {
            printf("%04lu  %-12s f=%u a0=%u\n", (unsigned long) idx, mnemonic, WYRM_F(cur), WYRM_A0(cur));
        } else {
            printf("%04lu  %-12s f=%u a0=%u a1=%u a2=%u\n",
                (unsigned long) idx, mnemonic, WYRM_F(cur), WYRM_A0(cur), WYRM_A1(cur), WYRM_A2(cur));
        }
        idx += words;
    }
}

static void print_usage(const char* argv0)
{
    fprintf(stderr, "Usage: %s [-I dir]... [-v] [--cache-dir DIR] [--sections] [--disasm]\n"
        "       %*s[-m mod::sub] [--check] [--build-bc [-o DIR] [--emit LIST] [--strip]]\n"
        "       %*sfile.wy|file.wyc|file.wyd [args...]\n"
        "       %*s-i    (interactive: read and run input from standard input)\n",
        argv0, (int) strlen(argv0), "", (int) strlen(argv0), "", (int) strlen(argv0), "");
}

/**
 * Build the `__ARGS` value: the arguments after the script path, as a list
 * of string values (`wyrm script.wyc a b c` -> `["a", "b", "c"]`).
 */
static wy_error make_args_list(wy_context* context, char** argv, int argc, wy_value* out)
{
    wy_list* list = WY_NULL;
    wy_error err = wy_list_new(context, argc > 0 ? (wy_uword) argc : 0, &list);
    if (err != WY_ERR_NONE) { return err; }
    for (int i = 0; i < argc; i++) {
        wy_string* str = WY_NULL;
        err = wy_string_new(context, argv[i], wy_strlen_f(argv[i]), &str);
        if (err != WY_ERR_NONE) { return err; }
        err = wy_list_push(context, list, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str));
        if (err != WY_ERR_NONE) { return err; }
    }
    *out = wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) list);
    return WY_ERR_NONE;
}

/**
 * Seed a host-supplied global (`__name__`, `__ARGS`) into a module's free
 * slot before its init runs. These are ordinary globals, not opcodes: the
 * module reads them through gget like any other name the builtins don't
 * supply. A module that never reads the name is untouched.
 */
static wy_error seed_global(wy_context* context, wy_module* module, const char* name, wy_value value)
{
    return wy_link_seed_global(context, module, name, value);
}

static wy_error seed_dunder_name_(wy_context* context, wy_module* module)
{
    wy_string* name = WY_NULL;
    wy_error err = wy_string_new(context, "__main__", 8, &name);
    if (err != WY_ERR_NONE) { return err; }
    return seed_global(context, module, "__name__",
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) name));
}

static bool ends_with_(const char* text, const char* suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

/**
 * Epic 11 M2/M3: run the embedded compiler's `compile_source` on
 * `compile_ctx` - the builtin table's wyrm::tools::compile_source, never a
 * filesystem lookup (that path would need the compiler to compile itself) -
 * and answer the fresh container bytes, allocated on `compile_ctx`. On
 * failure `*msg` names it for the CLI's error line (it points into the
 * context, so print before destroying anything).
 */
static wy_error compile_source_bytes_(wy_context* compile_ctx,
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

static wy_import_fs_search_path* compile_shim_search_ = WY_NULL;
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
#define WY_MAIN_MAX_COMPILE_DEPTH 8

static wy_error compile_on_scratch_(void* ud, wy_context* requester, const char* source_path,
    const wy_u8* source, wy_uword source_len, wy_u8** out_bytes, wy_uword* out_len, const char** msg)
{
    WY_UNUSED(ud);
    if (compile_scratch_depth_ >= WY_MAIN_MAX_COMPILE_DEPTH) {
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
    wy_fiber* fiber = wy_fiber_create(ctx, WY_MAIN_STACK_LEN, WY_MAIN_FRAME_COUNT);
    if (fiber == WY_NULL || wy_context_attach_fiber(ctx, fiber) != WY_ERR_NONE) {
        *msg = "out of memory creating the compile fiber"; err = WY_ERR_NOMEM; goto done;
    }
    ctx->import_hook = wy_import_fs_hook;
    ctx->import_ud = compile_shim_search_;

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

    err = compile_source_bytes_(ctx, source_path, source, source_len, &blob, &blob_len, msg);
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

/** The entry's basename without .wy - the compiler's module name. */
static size_t entry_stem_(const char* path, const char** out)
{
    const char* base = strrchr(path, '/');
    base = (base != WY_NULL) ? base + 1 : path;
    size_t len = strlen(base);
    if (len > 3 && memcmp(base + len - 3, ".wy", 3) == 0) { len -= 3; }
    *out = base;
    return len;
}

/** Parse --emit (a comma-separated subset of c,wyc,wyd; "wyc" spells the
 * port's .wyd container - the wyrm compiler never writes .wyc). "wya" is
 * rejected: image.wy's listing writer is pypoc-only today (known gap).
 * Returns false, with a message printed, for an unknown or empty entry. */
static bool parse_emit_(const char* arg, const char* names[3], wy_uword* count)
{
    *count = 0;
    const char* p = arg;
    while (*p != '\0') {
        const char* comma = strchr(p, ',');
        size_t token_len = (comma != WY_NULL) ? (size_t) (comma - p) : strlen(p);
        const char* mapped = WY_NULL;
        if (token_len == 1 && p[0] == 'c') { mapped = "c"; }
        else if ((token_len == 3 && memcmp(p, "wyc", 3) == 0) ||
                 (token_len == 3 && memcmp(p, "wyd", 3) == 0)) { mapped = "wyd"; }
        else if (token_len == 3 && memcmp(p, "wya", 3) == 0) {
            fprintf(stderr, "wyrm: --emit: .wy_a listings are not supported by the port's writer yet; use --disasm\n");
            return false;
        }
        if (mapped == WY_NULL) {
            fprintf(stderr, "wyrm: --emit: unknown container '%.*s'; expected a subset of c,wyc,wyd\n",
                (int) token_len, p);
            return false;
        }
        bool dup = false;
        for (wy_uword i = 0; i < *count; i++) { if (names[i] == mapped) { dup = true; } }
        if (!dup) { names[(*count)++] = mapped; }
        if (comma == WY_NULL) { break; }
        p = comma + 1;
    }
    return *count > 0;
}

/**
 * --build-bc (epic 11 M4): compile once in-process and write the requested
 * containers into out_dir. All compilation and file writing happens in the
 * embedded compile_source module's build_bc fn; C only shuttles strings.
 */
static wy_error build_bc_(wy_context* context, const char* path,
    const wy_u8* source, wy_uword source_len, const char* out_dir,
    const char* const* emit_names, wy_uword emit_count, const char** msg)
{
    static const char host_name[] = "wyrm::tools::compile_source";
    wy_error err = WY_ERR_NONE;

    wy_string* host_path = WY_NULL;
    err = wy_string_new(context, host_name, sizeof(host_name) - 1, &host_path);
    if (err != WY_ERR_NONE) { return err; }
    wy_module* host = WY_NULL;
    err = wy_link_import(context, host_path, &host);
    if (err != WY_ERR_NONE) { *msg = "cannot load the embedded compiler"; return err; }
    if (host->state == WY_MODULE_LOADED) {
        err = wy_module_run_init(context, host);
        if (err != WY_ERR_NONE) { *msg = "the embedded compiler failed to initialise"; return err; }
    }

    wy_symbol fn_sym = WY_NULL;
    err = wy_context_intern(context, "build_bc", sizeof("build_bc") - 1, &fn_sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_value* fn_slot = WY_NULL;
    err = wy_link_scope_member(wy_value_object(WY_TYPE_TAG_MODULE, (wy_object*) host), fn_sym, &fn_slot);
    if (err != WY_ERR_NONE || fn_slot == WY_NULL) { *msg = "the embedded compiler has no build_bc"; return WY_ERR_UNBOUND; }

    const char* stem = WY_NULL;
    size_t stem_len = entry_stem_(path, &stem);

    wy_string* src_str = WY_NULL;
    wy_string* name_str = WY_NULL;
    wy_string* path_str = WY_NULL;
    wy_string* out_str = WY_NULL;
    err = wy_string_new(context, (const char*) source, source_len, &src_str);
    if (err == WY_ERR_NONE) { err = wy_string_new(context, stem, stem_len, &name_str); }
    if (err == WY_ERR_NONE) { err = wy_string_new(context, path, strlen(path), &path_str); }
    if (err == WY_ERR_NONE) { err = wy_string_new(context, out_dir, strlen(out_dir), &out_str); }
    if (err != WY_ERR_NONE) { return err; }
    wy_list* emit_list = WY_NULL;
    if (err == WY_ERR_NONE) { err = wy_list_new(context, emit_count, &emit_list); }
    for (wy_uword i = 0; err == WY_ERR_NONE && i < emit_count; i++) {
        wy_string* item = WY_NULL;
        err = wy_string_new(context, emit_names[i], strlen(emit_names[i]), &item);
        if (err == WY_ERR_NONE) {
            err = wy_list_push(context, emit_list, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) item));
        }
    }
    if (err != WY_ERR_NONE) { return err; }

    wy_value args[5] = {
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) src_str),
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) name_str),
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) path_str),
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) out_str),
        wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) emit_list),
    };
    wy_value result = wy_value_nil();
    if (wy_context_root_push_f(context, &result) != WY_ERR_NONE) { return WY_ERR_NOMEM; }
    err = wy_vm_call_sync(context, *fn_slot, args, 5, &result, 1);
    if (err != WY_ERR_NONE) {
        wy_value fault = context->current_fiber->fault;
        *msg = (wy_value_is_error(fault) && fault.data.gc_object != WY_NULL &&
                ((wy_error_obj*) fault.data.gc_object)->what != WY_NULL)
            ? ((wy_error_obj*) fault.data.gc_object)->what->str : "build-bc fault";
    } else if (wy_value_is_error(result)) {
        wy_error_obj* failure = (wy_error_obj*) result.data.gc_object;
        *msg = (failure != WY_NULL && failure->what != WY_NULL) ? failure->what->str : "build-bc failed";
        err = WY_ERR_FAULT;
    }
    wy_context_root_pop_f(context);
    return err;
}

int main(int argc, char** argv)
{
    const char* file_path = WY_NULL;
    bool want_sections = false;
    bool want_disasm = false;
    bool want_verbose = false;
    bool want_check = false;
    bool want_build_bc = false;
    bool want_strip = false;  /* accepted; the port never emits a debug section */
    bool want_repl = false;   /* -i: the interactive loop (repl.c) */
    const char* module_arg = WY_NULL;   /* -m mod::sub */
    const char* emit_arg = WY_NULL;     /* --emit wya,c,wyd|wyc */
    const char* out_dir_arg = WY_NULL;  /* -o DIR with --build-bc */
    const char* cache_dir_arg = WY_NULL;
    const char* include_paths[WY_MAIN_MAX_INCLUDE];
    wy_uword include_count = 0;
    char** script_argv = WY_NULL;
    int script_argc = 0;

    for (int i = 1; i < argc; i++) {
        if (wy_strcmp_f(argv[i], "--sections") == 0) {
            want_sections = true;
        } else if (wy_strcmp_f(argv[i], "--disasm") == 0) {
            want_disasm = true;
        } else if (wy_strcmp_f(argv[i], "-v") == 0 || wy_strcmp_f(argv[i], "--verbose") == 0) {
            want_verbose = true;
        } else if (wy_strcmp_f(argv[i], "--check") == 0) {
            want_check = true;
        } else if (wy_strcmp_f(argv[i], "--build-bc") == 0) {
            want_build_bc = true;
        } else if (wy_strcmp_f(argv[i], "--strip") == 0) {
            want_strip = true;
        } else if (wy_strcmp_f(argv[i], "-i") == 0 && file_path == WY_NULL && module_arg == WY_NULL) {
            want_repl = true;
        } else if (wy_strcmp_f(argv[i], "--emit") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "wyrm: --emit requires a container list\n");
                print_usage(argv[0]);
                return 2;
            }
            i++;
            emit_arg = argv[i];
        } else if (wy_strcmp_f(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "wyrm: -o requires a directory argument\n");
                print_usage(argv[0]);
                return 2;
            }
            i++;
            out_dir_arg = argv[i];
        } else if (wy_strcmp_f(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "wyrm: -m requires a module path\n");
                print_usage(argv[0]);
                return 2;
            }
            i++;
            module_arg = argv[i];
        } else if (wy_strcmp_f(argv[i], "--cache-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "wyrm: --cache-dir requires a directory argument\n");
                print_usage(argv[0]);
                return 2;
            }
            i++;
            cache_dir_arg = argv[i];
        } else if (wy_strcmp_f(argv[i], "-I") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "wyrm: -I requires a path argument\n");
                print_usage(argv[0]);
                return 2;
            }
            i++;
            if (include_count >= WY_MAIN_MAX_INCLUDE) {
                fprintf(stderr, "wyrm: too many -I paths (max %u)\n", (unsigned) WY_MAIN_MAX_INCLUDE);
                return 2;
            }
            include_paths[include_count++] = argv[i];
        } else if (wy_strncmp_f(argv[i], "-I", 2) == 0) {
            if (include_count >= WY_MAIN_MAX_INCLUDE) {
                fprintf(stderr, "wyrm: too many -I paths (max %u)\n", (unsigned) WY_MAIN_MAX_INCLUDE);
                return 2;
            }
            include_paths[include_count++] = argv[i] + 2;
        } else if (file_path != WY_NULL || module_arg != WY_NULL) {
            /* Everything after the script path (or the -m module) is a
             * script argument, left untouched (leading dashes included),
             * as `__ARGS` - the known tool flags above are still honored
             * there for --sections/--disasm/check_disasm.sh usage. */
            if (script_argv == WY_NULL) { script_argv = &argv[i]; }
            script_argc++;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "wyrm: unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        } else {
            file_path = argv[i];
        }
    }

    if (module_arg != WY_NULL && (want_check || want_build_bc)) {
        fprintf(stderr, "wyrm: -m cannot combine with --check or --build-bc\n");
        return 2;
    }
    if (want_check && want_build_bc) {
        fprintf(stderr, "wyrm: --check cannot combine with --build-bc\n");
        return 2;
    }
    if ((emit_arg != WY_NULL || want_strip || out_dir_arg != WY_NULL) && !want_build_bc) {
        fprintf(stderr, "wyrm: --emit/--strip/-o require --build-bc\n");
        return 2;
    }
    if (want_repl && (file_path != WY_NULL || module_arg != WY_NULL || want_check || want_build_bc
            || want_sections || want_disasm)) {
        fprintf(stderr, "wyrm: -i takes no script and cannot combine with --check, --build-bc, -m, --sections or --disasm\n");
        return 2;
    }
    if (module_arg == WY_NULL && file_path == WY_NULL && !want_repl) {
        print_usage(argv[0]);
        return 2;
    }

    wy_machine* machine = wy_cmachine_new();
    if (machine == WY_NULL) {
        fprintf(stderr, "wyrm: failed to create machine\n");
        return 1;
    }

    wy_context* context = wy_cmachine_context_new(machine);
    if (context == WY_NULL) {
        fprintf(stderr, "wyrm: failed to create context\n");
        return 1;
    }

    wy_fiber* fiber = wy_fiber_create(context, WY_MAIN_STACK_LEN, WY_MAIN_FRAME_COUNT);
    if (fiber == WY_NULL) {
        fprintf(stderr, "wyrm: failed to create fiber\n");
        return 1;
    }
    if (wy_context_attach_fiber(context, fiber) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: failed to attach fiber\n");
        return 1;
    }

    context->io.write = io_write_stdout_;
    context->io.ud = WY_NULL;

    wy_import_fs_search_path search_path;
    wy_import_fs_search_path_init_s(&search_path);
    for (wy_uword i = 0; i < include_count; i++) {
        if (wy_import_fs_add_root(wy_context_get_machine(context)->allocator, &search_path, include_paths[i]) != WY_ERR_NONE) {
            fprintf(stderr, "wyrm: failed to install search path\n");
            return 1;
        }
    }
    /* Builtin module table (epic 11 M1): the embedded compiler + library
     * images, consulted after every -I root misses. The std::expand child
     * VM copies import_hook/import_ud, so it resolves wyrm::compiler::expand
     * and friends from the table with no file on disk. */
    search_path.builtins = wyrm_builtin_modules;
    search_path.builtin_count = wyrm_builtin_module_count;
    search_path.verbose = want_verbose;
    if (cache_dir_arg != WY_NULL) {
        /* The contract fixes --cache-dir as absolute; absolutize a
         * relative spelling against the cwd so the prefix mapping is
         * stable no matter where later runs start. */
        static char cache_dir_buffer[4096];
        if (cache_dir_arg[0] == '/') {
            search_path.cache_dir = cache_dir_arg;
        } else if (getcwd(cache_dir_buffer, sizeof(cache_dir_buffer)) != WY_NULL) {
            size_t at = strlen(cache_dir_buffer);
            if (at + 1 + strlen(cache_dir_arg) + 1 <= sizeof(cache_dir_buffer)) {
                cache_dir_buffer[at++] = '/';
                memcpy(cache_dir_buffer + at, cache_dir_arg, strlen(cache_dir_arg) + 1);
                search_path.cache_dir = cache_dir_buffer;
            } else {
                fprintf(stderr, "wyrm: --cache-dir path too long\n");
                return 1;
            }
        } else {
            fprintf(stderr, "wyrm: cannot resolve --cache-dir against the cwd\n");
            return 1;
        }
    }
    compile_shim_search_ = &search_path;
    search_path.compile = compile_on_scratch_;
    context->import_hook = wy_import_fs_hook;
    context->import_ud = &search_path;

    /* Epic 11 M4: --emit/--strip validation and the default (binary + C
     * source; .wy_a listings are a known gap, see parse_emit_). */
    const char* emit_names[3];
    wy_uword emit_count = 0;
    if (want_build_bc) {
        if (emit_arg == WY_NULL) {
            emit_names[0] = "wyd"; emit_names[1] = "c";
            emit_count = 2;
        } else if (!parse_emit_(emit_arg, emit_names, &emit_count)) {
            return 2;
        }
    }

    wy_module* builtins = WY_NULL;
    if (wy_builtins_new(context, &builtins) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: failed to build builtins module\n");
        return 1;
    }
    context->builtins = builtins;

    if (wy_io_natives_install(context) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: failed to install the std::io natives\n");
        return 1;
    }

    if (wy_expand_module_install(context) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: failed to install std::expand\n");
        return 1;
    }

    if (want_repl) { return wyrm_repl(context, isatty(STDIN_FILENO) != 0); }

    wy_module* module = WY_NULL;
    wy_error last_error = WY_ERR_NONE;

    if (module_arg != WY_NULL) {
        /* -m mod::sub (epic 11 M4): resolve through the search path
         * (roots, cache, builtin table), run its init. __name__ and
         * __ARGS are seeded below like python -m. */
        wy_string* path_str = WY_NULL;
        if (wy_string_new(context, module_arg, strlen(module_arg), &path_str) != WY_ERR_NONE) {
            fprintf(stderr, "wyrm: out of memory\n");
            return 1;
        }
        last_error = wy_link_import(context, path_str, &module);
        if (last_error != WY_ERR_NONE) {
            fprintf(stderr, "wyrm: -m %s: cannot load module (error %d)\n", module_arg, (int) last_error);
            return 1;
        }
    } else if (want_check) {
        /* --check (epic 11 M4): compile/validate only, never run. Single
         * file (pypoc's recursive import walk is not ported; imports the
         * compiled module itself pulls in do run through the hook during
         * decorator expansion, so transitive compile errors surface). */
        wy_uword file_size = 0;
        wy_u8* file_content = wy_import_fs_read_file(context, file_path, &file_size);
        if (file_content == WY_NULL) {
            fprintf(stderr, "wyrm: %s: failed to read file\n", file_path);
            return 1;
        }
        if (ends_with_(file_path, ".wy")) {
            const char* compile_msg = WY_NULL;
            wy_u8* blob = WY_NULL;
            wy_uword blob_len = 0;
            last_error = compile_source_bytes_(context, file_path, file_content, file_size, &blob, &blob_len, &compile_msg);
            if (last_error == WY_ERR_NONE) {
                last_error = wy_module_load_bytes(context, blob, blob_len, true, &module);
                if (last_error != WY_ERR_NONE) { compile_msg = WY_NULL; }
            }
            if (last_error != WY_ERR_NONE) {
                fprintf(stderr, "wyrm: %s: %s\n", file_path,
                    compile_msg != WY_NULL ? compile_msg : "failed to compile");
                return 1;
            }
        } else {
            last_error = wy_module_load_bytes(context, file_content, file_size, true, &module);
            if (last_error != WY_ERR_NONE) {
                fprintf(stderr, "wyrm: %s: failed to load module (error %d)\n", file_path, (int) last_error);
                return 1;
            }
        }
        wy_context_set_root(context, module);
        return 0;
    } else if (want_build_bc) {
        /* --build-bc (epic 11 M4): compile to containers, write them, run
         * nothing. The binary container is .wyd (the wyrm compiler never
         * writes .wyc - that is pypoc's provenance). */
        if (!ends_with_(file_path, ".wy")) {
            fprintf(stderr, "wyrm: --build-bc needs a .wy source, not '%s'\n", file_path);
            return 2;
        }
        char out_dir[4096];
        if (out_dir_arg != WY_NULL) {
            snprintf(out_dir, sizeof(out_dir), "%s", out_dir_arg);
            if (!wy_import_cache_ensure_dir(out_dir)) {
                fprintf(stderr, "wyrm: %s: cannot create output directory\n", out_dir);
                return 1;
            }
        } else {
            const char* last = strrchr(file_path, '/');
            if (last == WY_NULL) { snprintf(out_dir, sizeof(out_dir), "."); }
            else if (last == file_path) { snprintf(out_dir, sizeof(out_dir), "/"); }
            else { snprintf(out_dir, sizeof(out_dir), "%.*s", (int) (last - file_path), file_path); }
        }
        wy_uword file_size = 0;
        wy_u8* file_content = wy_import_fs_read_file(context, file_path, &file_size);
        if (file_content == WY_NULL) {
            fprintf(stderr, "wyrm: %s: failed to read file\n", file_path);
            return 1;
        }
        const char* build_msg = WY_NULL;
        last_error = build_bc_(context, file_path, file_content, file_size, out_dir,
            emit_names, emit_count, &build_msg);
        if (last_error != WY_ERR_NONE) {
            fprintf(stderr, "wyrm: %s: %s\n", file_path,
                build_msg != WY_NULL ? build_msg : "failed to build");
            return 1;
        }
        return 0;
    }

    if (module_arg == WY_NULL) {
        wy_uword file_size = 0;
        wy_u8* file_content = wy_import_fs_read_file(context, file_path, &file_size);
        if (file_content == WY_NULL) {
            fprintf(stderr, "wyrm: %s: failed to read file\n", file_path);
            return 1;
        }

        if (ends_with_(file_path, ".wy")) {
            /* Epic 11 M3: the entry follows the same cache rules as every
             * import - a valid __wycache__ (or --cache-dir) .wyd is loaded
             * directly; a miss compiles and stores best-effort. */
            wy_allocator* allocator = wy_context_get_machine(context)->allocator;
            double source_mtime = 0;
            bool have_mtime = wy_import_cache_source_mtime(file_path, &source_mtime);
            char* cache_path = have_mtime ? wy_import_cache_path(allocator, search_path.cache_dir, file_path) : WY_NULL;

            if (cache_path != WY_NULL && wy_import_cache_valid(cache_path, source_mtime)) {
                wy_uword cached_len = 0;
                wy_u8* cached = wy_import_fs_read_file(context, cache_path, &cached_len);
                if (cached != WY_NULL) {
                    if (search_path.verbose) { fprintf(stderr, "wyrm: cache %s\n", cache_path); }
                    last_error = wy_module_load_bytes(context, cached, cached_len, true, &module);
                    if (last_error != WY_ERR_NONE) {
                        fprintf(stderr, "wyrm: %s: cached image failed to load (error %d)\n", cache_path, (int) last_error);
                        return 1;
                    }
                }
            }

            if (module == WY_NULL) {
                const char* compile_msg = WY_NULL;
                wy_u8* blob = WY_NULL;
                wy_uword blob_len = 0;
                last_error = compile_source_bytes_(context, file_path, file_content, file_size, &blob, &blob_len, &compile_msg);
                if (last_error != WY_ERR_NONE) {
                    if (compile_msg != WY_NULL) {
                        fprintf(stderr, "wyrm: %s: %s\n", file_path, compile_msg);
                    } else {
                        fprintf(stderr, "wyrm: %s: failed to compile (error %d)\n", file_path, (int) last_error);
                    }
                    return 1;
                }
                if (search_path.verbose) {
                    struct stat st;
                    const char* why = (cache_path != WY_NULL && stat(cache_path, &st) == 0) ? "stale" : "absent";
                    fprintf(stderr, "wyrm: jit %s (cache %s)\n", file_path, why);
                    const char* store_reason = WY_NULL;
                    bool stored = cache_path != WY_NULL &&
                        wy_import_cache_store(allocator, cache_path, blob, blob_len, &store_reason);
                    if (stored) { fprintf(stderr, "wyrm: cache write %s\n", cache_path); }
                    else { fprintf(stderr, "wyrm: cache write skipped (%s)\n", store_reason != WY_NULL ? store_reason : "no cache path"); }
                } else if (cache_path != WY_NULL) {
                    (void) wy_import_cache_store(allocator, cache_path, blob, blob_len, WY_NULL);
                }
                last_error = wy_module_load_bytes(context, blob, blob_len, true, &module);
                if (last_error != WY_ERR_NONE) {
                    fprintf(stderr, "wyrm: %s: failed to load compiled module (error %d)\n", file_path, (int) last_error);
                    return 1;
                }
            }
            if (cache_path != WY_NULL) { wy_allocator_free(allocator, cache_path); }
        } else {
            last_error = wy_module_load_bytes(context, file_content, file_size, true, &module);
            if (last_error != WY_ERR_NONE) {
                fprintf(stderr, "wyrm: %s: failed to load module (error %d)\n", file_path, (int) last_error);
                return 1;
            }
        }
    }
    wy_context_set_root(context, module);

    if (want_sections) { print_sections(module); return 0; }
    if (want_disasm) { print_disasm(module); return 0; }

    const char* entry_name = (module_arg != WY_NULL) ? module_arg : file_path;
    wy_error seed_error = seed_dunder_name_(context, module);
    if (seed_error == WY_ERR_NONE) {
        wy_value args_value = wy_value_nil();
        seed_error = make_args_list(context, script_argv, script_argc, &args_value);
        if (seed_error == WY_ERR_NONE) {
            seed_error = seed_global(context, module, "__ARGS", args_value);
        }
    }
    if (seed_error != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: %s: failed to seed host globals (error %d)\n", entry_name, (int) seed_error);
        return 1;
    }

    last_error = wy_module_run_init(context, module);
    if (last_error == WY_ERR_NONE) {
        return 0;
    }
    if (last_error == WY_ERR_FAULT) {
        wy_value fault = context->current_fiber->fault;
        if (wy_value_is_error(fault) && fault.data.gc_object != WY_NULL) {
            wy_error_obj* err = (wy_error_obj*) fault.data.gc_object;
            if (err->what != WY_NULL) {
                fprintf(stderr, "wyrm: %s: fault: %s\n", entry_name, err->what->str);
                return 1;
            }
        }
        fprintf(stderr, "wyrm: %s: fault\n", entry_name);
        return 1;
    }
    fprintf(stderr, "wyrm: %s: failed to run module (error %d)\n", entry_name, (int) last_error);
    return 1;
}