#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/platform/hosted/allocator_cmem.h>

wy_machine* wy_cmachine_new(void)
{
    wyrm_allocator_cmem* allocator = wyrm_allocator_cmem_new();
    if (allocator == WYRM_NULL) { return WYRM_NULL; }

    wyrm_allocator* machine_allocator = wyrm_allocator_from_cmem(allocator);
    wyrm_machine* machine = wyrm_allocator_alloc(machine_allocator, sizeof(wyrm_machine));
    if (machine == WYRM_NULL) { wyrm_allocator_cmem_destroy(allocator); return WYRM_NULL; }

    wyrm_machine_init_s(machine, machine_allocator);
    return machine;
}


void wy_cmachine_destroy(wy_machine* machine)
{
    if (machine == WYRM_NULL) { return; }

    wyrm_allocator* machine_allocator = machine->allocator;
    wyrm_machine_finalize_f(machine);

    wyrm_allocator_free(machine_allocator, machine);
    wyrm_allocator_cmem_destroy((wyrm_allocator_cmem*) machine_allocator);
}


wy_context* wy_cmachine_context_new(wy_machine* machine)
{
    if (machine == WYRM_NULL) { return WYRM_NULL; }

    wy_context* ctx = wyrm_allocator_alloc(machine->allocator, sizeof(wy_context));
    if (ctx == WYRM_NULL) { return WYRM_NULL; }

    wy_error last_error = wyrm_context_init_s(ctx);
    if (last_error != WYRM_ERR_NONE) { return WYRM_NULL; }

    last_error = wyrm_machine_attach_context(machine, ctx);
    if (last_error != WYRM_ERR_NONE) {
        wyrm_context_finalize_f(ctx);
        wyrm_allocator_free(machine->allocator, ctx);
        return WYRM_NULL;
    }

    return ctx;
}


void wy_cmachine_context_destroy(wy_context* ctx)
{
    if (ctx == WYRM_NULL) { return; }
    wyrm_machine* machine = wyrm_context_get_machine(ctx);

    wyrm_context_finalize_f(ctx);
    wyrm_allocator_free(machine->allocator, ctx);
}
