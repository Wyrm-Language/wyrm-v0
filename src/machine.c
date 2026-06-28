#include <wyrm.h>

wyrm_error wyrm_machine_init_s(wyrm_machine* self, wyrm_allocator* alloc)
{
    self->allocator = alloc;
    self->context= WYRM_NULL;
    return WYRM_ERR_NONE;
}

wyrm_error wyrm_machine_attach_context(wyrm_machine* self, wyrm_context* context)
{
    if (self == WYRM_NULL || context == WYRM_NULL) { return WYRM_ERR_INVAL; }
    self->context = context;
    context->parent = self;
    return WYRM_ERR_NONE;
}
