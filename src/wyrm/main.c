#include <wyrm.h>
#include <stdio.h>

#include <wyrm/image.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/opcode_names.h>
#include <wyrm/platform/hosted/cmachine.h>

/**
 * Read a whole file into a buffer allocated through `context`'s allocator
 * (never raw malloc, so the module can own and free it the same way it
 * frees everything else it holds).
 */
static wy_u8* read_file(wy_context* context, const char* path, wy_uword* out_size)
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
    fprintf(stderr, "Usage: %s [--sections] [--disasm] file.wyc\n", argv0);
}

int main(int argc, char** argv)
{
    const char* file_path = WY_NULL;
    bool want_sections = false;
    bool want_disasm = false;

    for (int i = 1; i < argc; i++) {
        if (wy_strcmp_f(argv[i], "--sections") == 0) {
            want_sections = true;
        } else if (wy_strcmp_f(argv[i], "--disasm") == 0) {
            want_disasm = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "wyrm: unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        } else if (file_path == WY_NULL) {
            file_path = argv[i];
        } else {
            fprintf(stderr, "wyrm: unexpected argument %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
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

    wy_uword file_size = 0;
    wy_u8* file_content = read_file(context, file_path, &file_size);
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

    if (want_sections) { print_sections(module); }
    if (want_disasm) { print_disasm(module); }

    if (!want_sections && !want_disasm) {
        printf("not run: interpreter lands in epic 2\n");
    }

    return 0;
}
