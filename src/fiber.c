#include <wyrm.h>
#include <wyrm/internal_api.h>

wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len)
{
    wyrm_fiber* fiber = wyrm_context_gc_alloc(context, sizeof(wyrm_fiber));
    if (fiber == WYRM_NULL) { return WYRM_NULL; }

    wyrm_value* stack = wyrm_context_gc_alloc(context, stack_len * sizeof(wyrm_value));
    if (stack == WYRM_NULL) {
        wyrm_context_gc_free(context, fiber);
        return WYRM_NULL;
    }

    fiber->parent = WYRM_NULL;
    wyrm_stack_init_f(&fiber->value_stack, stack, stack_len);
    wyrm_context_gc_init(context, &fiber->obj, WYRM_TYPE_TAG_FIBER);
    return fiber;
}

void wyrm_fiber_finalize_f(wyrm_fiber* self)
{
    wyrm_context_gc_free(self->parent, self->value_stack.entries_begin);
    self->value_stack.entries_begin = WYRM_NULL;
    self->value_stack.entries_end = WYRM_NULL;
    self->value_stack.base = WYRM_NULL;
    self->value_stack.top = WYRM_NULL;
}
