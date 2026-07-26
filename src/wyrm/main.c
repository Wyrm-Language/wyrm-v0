#include <wyrm.h>
#include <stdio.h>

#include <wy.h>

wyrm_exec_state w_main(wyrm_state* state)
{
    printf("Last of the call stack, expect values = 0 actual = %ld\n", wyrm_state_value_count(state));
    return WYRM_EXEC_DONE;
}

wyrm_exec_state w_print_int(wyrm_state* state)
{
    wyrm_value* a = wyrm_state_value_n(state, 0);
    printf("w_print_int: %ld\n", a->data.word);
    return WYRM_EXEC_DONE;
}

wyrm_exec_state w_mul_int(wyrm_state* state)
{
    wyrm_value* a = wyrm_state_value_n(state, 0);
    wyrm_value* b = wyrm_state_value_n(state, 1);

    wyrm_state_push_return(state, wyrm_value_word(a->data.word * b->data.word));
    printf("w_mul_int: %ld x %ld\n", a->data.word, b->data.word);;
    return WYRM_EXEC_DONE;
}

wyrm_exec_state w_do_a_mul(wyrm_state* state)
{
    WYRM_UNUSED(state);
    printf("pushing arguments\n");
    wyrm_value v_ints[2] = {
        { .type = WYRM_TYPE_TAG_WORD, .data.word = 8 },
        {.type = WYRM_TYPE_TAG_WORD, .data.word = 32 },
    };

    wyrm_state_call_continue(state, w_print_int, w_mul_int, v_ints, 2);
    return WYRM_EXEC_CONTINUE;
}

int main(void) {
    wyrm_error last_error = WYRM_ERR_NONE;

    wy_options machine_options = {
        .allocator = WY_ALLOCATOR_CMEM
    };

    wy_ctx* ctx = wy_init(&machine_options);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize context\n");
        return -1;
    }

    // TODO: add wy_eval or something...
    wyrm_fiber* fiber = wy_get_primary_fiber(ctx);
    wyrm_fiber_push_continuation(fiber, w_main);
    wyrm_fiber_push_continuation(fiber, w_do_a_mul);

    last_error = wyrm_context_activate(
        wy_get_primary_context(ctx),
        fiber);

    if (last_error != WYRM_ERR_NONE) {
        fprintf(stderr, "Failed to activate primary context: %d\n", (int)(last_error));
        return -1;
    }

    int result = wy_run(ctx);
    wy_destroy(ctx);

    return result;
}
