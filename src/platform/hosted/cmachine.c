#include <wyrm/platform/hosted/cmachine.h>
#include <wyrm/platform/hosted/allocator_cmem.h>
#include <wyrm/context.h>

wy_machine* wy_cmachine_new(void)
{
    wy_allocator_cmem* allocator = wy_allocator_cmem_new();
    if (allocator == WY_NULL) { return WY_NULL; }

    wy_allocator* machine_allocator = wy_allocator_from_cmem(allocator);
    wy_machine* machine = wy_allocator_alloc(machine_allocator, sizeof(wy_machine));
    if (machine == WY_NULL) { wy_allocator_cmem_destroy(allocator); return WY_NULL; }

    wy_machine_init_s(machine, machine_allocator);
    return machine;
}


wy_uword wy_cmachine_destroy_residual(wy_machine* machine)
{
    if (machine == WY_NULL) { return 0; }

    wy_allocator* machine_allocator = machine->allocator;
    wy_machine_finalize_f(machine);

    wy_allocator_free(machine_allocator, machine);
    wy_uword residual = ((wy_allocator_cmem*) machine_allocator)->active_size;
    wy_allocator_cmem_destroy((wy_allocator_cmem*) machine_allocator);
    return residual;
}


void wy_cmachine_destroy(wy_machine* machine)
{
    (void) wy_cmachine_destroy_residual(machine);
}


wy_context* wy_cmachine_context_new(wy_machine* machine)
{
    if (machine == WY_NULL) { return WY_NULL; }
    if (machine->context != WY_NULL) { return WY_NULL; }

    wy_context* ctx = wy_allocator_alloc(machine->allocator, sizeof(wy_context));
    if (ctx == WY_NULL) { return WY_NULL; }

    wy_context_init_s(ctx);
    wy_error last_error = wy_machine_attach_context(machine, ctx);
    if (last_error != WY_ERR_NONE) {
        wy_context_finalize_f(ctx);
        wy_allocator_free(machine->allocator, ctx);
        return WY_NULL;
    }

    return ctx;
}


void wy_cmachine_context_destroy(wy_context* ctx)
{
    if (ctx == WY_NULL) { return; }
    wy_machine* machine = wy_context_get_machine(ctx);

    wy_context_finalize_f(ctx);
    wy_allocator_free(machine->allocator, ctx);
}
