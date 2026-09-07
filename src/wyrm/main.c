#include <wyrm.h>
#include <stdio.h>
#include <stdlib.h>

#include <wyrm/bson.h>
#include <wyrm/module.h>
#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/fiber.h>

wy_exec_state w_main(wy_state* state)
{
    printf("Last of the call stack, expect values = 0 actual = %ld\n", wy_state_value_count(state));
    return WY_EXEC_DONE;
}

wy_exec_state w_print_int(wy_state* state)
{
    wy_value* a = wy_state_value_n(state, 0);
    printf("w_print_int: %ld\n", a->data.word);
    return WY_EXEC_DONE;
}

wy_exec_state w_mul_int(wy_state* state)
{
    wy_value* a = wy_state_value_n(state, 0);
    wy_value* b = wy_state_value_n(state, 1);

    wy_state_push_return(state, wy_value_word(a->data.word * b->data.word));
    printf("w_mul_int: %ld x %ld\n", a->data.word, b->data.word);;
    return WY_EXEC_DONE;
}

wy_exec_state w_do_a_mul(wy_state* state)
{
    WY_UNUSED(state);
    printf("pushing arguments\n");
    wy_value v_ints[2] = {
        { .type = WY_TYPE_TAG_WORD, .data.word = 8 },
        {.type = WY_TYPE_TAG_WORD, .data.word = 32 },
    };

    wy_state_call_continue(state, w_print_int, w_mul_int, v_ints, 2);
    return WY_EXEC_CONTINUE;
}


char* get_file_content(const char* path, wy_uword* out_file_size)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    char* data = malloc(file_size + 1);
    fread(data, file_size, 1, fp);
    data[file_size] = '\0';
    *out_file_size = file_size;

    fclose(fp);
    return data;
}


int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: wyrm <file>\n");
        return -1;
    }

    const char* file_path = argv[1];
    wy_uword file_size = 0;
    char* file_content = get_file_content(file_path, &file_size);
    if (!file_content) {
        fprintf(stderr, "Failed to read file: %s\n", file_path);
        return -1;
    }

    wy_machine* machine = wy_cmachine_new();
    if (!machine) {
        fprintf(stderr, "Failed to create machine\n");
        return -1;
    }

    wy_context* context = wy_cmachine_context_new(machine);
    if (context == WY_NULL) {
        fprintf(stderr, "Failed to create context");
        return -1;
    }

    wy_module* mod = wy_module_new_f(context);
    if (wy_module_load(context, mod, (wy_u8*) file_content, file_size) != WY_ERR_NONE) {
        fprintf(stderr, "Failed to load module\n");
        return -1;
    }
    wy_context_set_root(context, mod);

    wy_fiber* fiber = wy_fiber_create(context, 8192, 1024);
    if (fiber == WY_NULL) {
        fprintf(stderr, "Failed to create fiber\n");
        return -1;
    }
    wy_context_attach_fiber(context, fiber);



    // TODO: add wy_eval or something...

    wy_fiber_push_continuation(fiber, w_main);
    wy_fiber_push_continuation(fiber, w_do_a_mul);

    wy_state state;
    wy_state_init_from_context_f(&state, context);
    state.fiber = fiber;

    wy_fiber_exec_f(fiber, &state);


    return 0;
}
