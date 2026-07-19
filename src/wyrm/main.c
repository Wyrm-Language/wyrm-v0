#include <wyrm.h>
#include <stdio.h>

#include "wyrm/platform/hosted/allocator_cmem.h"

#define STACK_SIZE 1024


wyrm_exec_result w_mul_int(wyrm_state* state)
{
    wyrm_value* a = wyrm_state_value_n(state, 0);
    wyrm_value* b = wyrm_state_value_n(state, 1);

    wyrm_state_push(state, wyrm_make_int(a->data.word * b->data.word));
    printf("w_mul_int: %ld\n", wyrm_state_value_n(state, 2)->data.word);
    return wyrm_make_exec_result(WYRM_EXEC_DONE, 1);
}

wyrm_exec_result w_add_int_x2(wyrm_state* state)
{
    wyrm_value* a = wyrm_state_value_n(state, 0);
    wyrm_value* b = wyrm_state_value_n(state, 1);

    wyrm_state_push(state, wyrm_make_int(a->data.word + b->data.word));
    printf("w_add_int: %ld\n", wyrm_state_value_n(state, 2)->data.word);
    wyrm_fiber_set_entry_f(state->fiber, w_mul_int);
    return wyrm_make_exec_result(WYRM_EXEC_DELEGATE, 2);
}

wyrm_exec_result w_print_int(wyrm_state* state)
{
    wyrm_value* a = wyrm_state_value_n(state, 0);
    printf("w_print_int: %ld\n", a->data.word);
    return wyrm_make_exec_result(WYRM_EXEC_DONE, 0);
}


wyrm_exec_result w_main(wyrm_state* state)
{
    printf("Last of the call stack, expect values = 0 actual = %ld\n", wyrm_state_value_count(state));
    return wyrm_make_exec_result(WYRM_EXEC_DONE, 0);
}

int main(void) {
    wyrm_allocator_cmem allocator;
    wyrm_allocator_cmem_init(&allocator);

    wyrm_value stack[STACK_SIZE];

    wyrm_fiber fiber;
    wyrm_fiber_init(&fiber, stack, STACK_SIZE);

    wyrm_fiber_push_continuation(&fiber, w_main, WYRM_NULL, 0);
    wyrm_fiber_push_continuation(&fiber, w_print_int, WYRM_NULL, 0);

    wyrm_fiber_push_value_f(&fiber, wyrm_make_int(1));
    wyrm_fiber_push_value_f(&fiber, wyrm_make_int(2));
    wyrm_fiber_set_entry_f(&fiber, w_add_int_x2);

    wyrm_state state;
    wyrm_state_init_s(&state);
    state.fiber = &fiber;

    wyrm_state_exec(&state);

    return 0;
}
