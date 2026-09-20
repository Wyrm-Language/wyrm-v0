#include <wyrm.h>
#include <stdio.h>

#include <wyrm/builtins.h>
#include <wyrm/error.h>
#include <wyrm/fiber.h>
#include <wyrm/image.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/opcode_names.h>
#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/platform/hosted/import_fs.h>
#include <wyrm/platform/hosted/io_native.h>
#include <wyrm/slot.h>
#include <wyrm/string.h>

enum { WY_MAIN_STACK_LEN = 4096, WY_MAIN_FRAME_COUNT = 256, WY_MAIN_MAX_INCLUDE = 64 };

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
    fprintf(stderr, "Usage: %s [-I dir]... [--sections] [--disasm] file.wyc [args...]\n", argv0);
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
    wy_symbol sym = WY_NULL;
    wy_error err = wy_context_intern(context, name, wy_strlen_f(name), &sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_uword slot = wy_slot_dict_get(&module->free_names, sym);
    if (slot == WY_SLOT_INVALID) { return WY_ERR_NONE; }
    module->globals[slot] = value;
    if (module->fill_layer != WY_NULL) { module->fill_layer[slot] &= WY_LINK_LAYER_MASK; }
    return WY_ERR_NONE;
}

static wy_error seed_dunder_name_(wy_context* context, wy_module* module)
{
    wy_string* name = WY_NULL;
    wy_error err = wy_string_new(context, "__main__", 8, &name);
    if (err != WY_ERR_NONE) { return err; }
    return seed_global(context, module, "__name__",
        wy_value_object(WY_TYPE_TAG_STR, (wy_object*) name));
}

int main(int argc, char** argv)
{
    const char* file_path = WY_NULL;
    bool want_sections = false;
    bool want_disasm = false;
    const char* include_paths[WY_MAIN_MAX_INCLUDE];
    wy_uword include_count = 0;
    char** script_argv = WY_NULL;
    int script_argc = 0;

    for (int i = 1; i < argc; i++) {
        if (wy_strcmp_f(argv[i], "--sections") == 0) {
            want_sections = true;
        } else if (wy_strcmp_f(argv[i], "--disasm") == 0) {
            want_disasm = true;
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
        } else if (file_path != WY_NULL) {
            /* Everything after the script path is a script argument, left
             * untouched (leading dashes included), as `__ARGS` - the known
             * tool flags above are still honored there for --sections/
             * --disasm/check_disasm.sh usage. */
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

    if (file_path == WY_NULL) {
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
    context->import_hook = wy_import_fs_hook;
    context->import_ud = &search_path;

    wy_module* builtins = WY_NULL;
    if (wy_builtins_new(context, &builtins) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: failed to build builtins module\n");
        return 1;
    }
    context->builtins = builtins;

    if (wy_io_module_install(context) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: failed to install std::io\n");
        return 1;
    }

    wy_uword file_size = 0;
    wy_u8* file_content = wy_import_fs_read_file(context, file_path, &file_size);
    if (file_content == WY_NULL) {
        fprintf(stderr, "wyrm: %s: failed to read file\n", file_path);
        return 1;
    }

    wy_module* module = WY_NULL;
    wy_error last_error = wy_module_load_bytes(context, file_content, file_size, true, &module);
    if (last_error != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: %s: failed to load module (error %d)\n", file_path, (int) last_error);
        return 1;
    }
    wy_context_set_root(context, module);

    if (want_sections) { print_sections(module); return 0; }
    if (want_disasm) { print_disasm(module); return 0; }

    wy_error seed_error = seed_dunder_name_(context, module);
    if (seed_error == WY_ERR_NONE) {
        wy_value args_value = wy_value_nil();
        seed_error = make_args_list(context, script_argv, script_argc, &args_value);
        if (seed_error == WY_ERR_NONE) {
            seed_error = seed_global(context, module, "__ARGS", args_value);
        }
    }
    if (seed_error != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: %s: failed to seed host globals (error %d)\n", file_path, (int) seed_error);
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
                fprintf(stderr, "wyrm: %s: fault: %s\n", file_path, err->what->str);
                return 1;
            }
        }
        fprintf(stderr, "wyrm: %s: fault\n", file_path);
        return 1;
    }
    fprintf(stderr, "wyrm: %s: failed to run module (error %d)\n", file_path, (int) last_error);
    return 1;
}