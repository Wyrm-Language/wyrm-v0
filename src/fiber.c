#include <wyrm.h>
#include <wyrm/internal_api.h>

void wyrm_fiber_init(wyrm_fiber* self, wyrm_value* stack, wyrm_uword stack_size)
{
    self->parent = WYRM_NULL;
    wyrm_stack_init_f(&self->value_stack, stack, stack_size);
}
