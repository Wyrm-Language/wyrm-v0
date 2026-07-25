#include <wy.h>
#include <wyrm.h>

#include <wyrm/platform/hosted/allocator_cmem.h>
#include <wyrm/platform/glib/mainloop.h>

#include "priv_wy_ctx.h"
#include "builtin/sys/wsys.h"

#define DEFAULT_STACK_LEN 1024

#include <stdlib.h>

static void destroy_cmem(void* data)
{
    free(data);
}

static void destroy_main_loop(void* data)
{
    wyrm_main_loop* main_loop = (wyrm_main_loop*) data;
    wyrm_glib_mainloop_destroy(main_loop);
}


wy_ctx* wy_init(const wy_options* options)
{
    wyrm_error last_error = WYRM_ERR_NONE;
    if (options == WYRM_NULL) { return WYRM_NULL; }

    // Grab Allocator
    // TODO: Allow selection
    if (options->allocator != WY_ALLOCATOR_CMEM) { return WYRM_NULL; }
    wyrm_allocator_cmem* cmem_allocator = malloc(sizeof(wyrm_allocator_cmem));
    if (cmem_allocator == WYRM_NULL) { return WYRM_NULL; }
    wyrm_allocator_cmem_init(cmem_allocator);
    wyrm_allocator* allocator = wyrm_allocator_from_cmem(cmem_allocator);
    wy_internal_destructor alloc_destructor = destroy_cmem;

    // Allocate context and initialize
    wy_ctx* ctx = wyrm_allocator_alloc(allocator, sizeof(wy_ctx));
    if (ctx == WYRM_NULL) {
        if (alloc_destructor != WYRM_NULL) {
            alloc_destructor(cmem_allocator);
        }
        return WYRM_NULL;
    }

    // Initialize all fields and data within primary_state
    wyrm_memset(ctx, 0, sizeof(wy_ctx));
    wyrm_state_init_s(&ctx->primary_state);
    ctx->allocator_destructor = alloc_destructor;
    ctx->allocator_primary_mem = (void*) cmem_allocator;
    ctx->allocator = allocator;
    ctx->primary_main_loop = WYRM_NULL;
    ctx->primary_main_loop_mem = WYRM_NULL;
    ctx->primary_main_loop_destructor = WYRM_NULL;

    // Initialize machine
    last_error = wyrm_machine_init_s(&ctx->machine, ctx->allocator);
    if (last_error != WYRM_ERR_NONE) { goto ctx_machine_destroy; }
    ctx->primary_state.machine = &ctx->machine;

    // Initialize main loop
    ctx->primary_main_loop = wyrm_glib_mainloop_new(ctx->allocator);
    ctx->primary_main_loop_mem = ctx->primary_main_loop;
    ctx->primary_main_loop_destructor = destroy_main_loop;

    // Initialize context
    last_error = wyrm_context_init_s(&ctx->primary_context, ctx->primary_main_loop);
    if (last_error != WYRM_ERR_NONE) { goto ctx_machine_destroy; }
    ctx->primary_state.context = &ctx->primary_context;
    last_error = wyrm_machine_attach_context(&ctx->machine, &ctx->primary_context);
    if (last_error != WYRM_ERR_NONE) { goto ctx_machine_destroy; }

    // Create main task fiber
    ctx->primary_state.fiber = wyrm_fiber_create(&ctx->primary_context, DEFAULT_STACK_LEN);
    if (ctx->primary_state.fiber == WYRM_NULL) { goto ctx_machine_destroy; }

    // Place stop continuation as last task on the thread
    if (wyrm_fiber_push_continuation(ctx->primary_state.fiber, wyrm_mod_sys_stop, WYRM_NULL, 0) != WYRM_ERR_NONE) {
        goto ctx_machine_destroy;
    }

    if (wyrm_context_attach_fiber(ctx->primary_state.context, ctx->primary_state.fiber) != WYRM_ERR_NONE) {
        goto ctx_machine_destroy;
    }
    return ctx;

ctx_machine_destroy:
    wy_destroy(ctx);
    return WYRM_NULL;
}

wyrm_state* wy_get_primary_state(wy_ctx* ctx)
{
    if (ctx == WYRM_NULL) { return WYRM_NULL; }
    return &ctx->primary_state;
}

struct wyrm_context* wy_get_primary_context(wy_ctx* ctx)
{
    if (ctx == WYRM_NULL) { return WYRM_NULL; }
    return ctx->primary_state.context;
}

struct wyrm_fiber* wy_get_primary_fiber(wy_ctx* ctx)
{
    if (ctx == WYRM_NULL) { return WYRM_NULL; }
    return ctx->primary_state.fiber;
}


int wy_run(wy_ctx* ctx)
{
    wyrm_main_loop_run(ctx->primary_main_loop);
    return 0;
}

void wy_destroy(wy_ctx* ctx)
{
    if (ctx == WYRM_NULL) { return; }

    wy_internal_destructor alloc_destructor = ctx->allocator_destructor;
    void* alloc_mem = ctx->allocator_primary_mem;
    wyrm_allocator* allocator = ctx->allocator_primary_mem;

    // Destroy context
    if (ctx->primary_state.context != WYRM_NULL) {
        wyrm_context_finalize_f(&ctx->primary_context);
    }

    // Destroy main loop
    if (ctx->primary_main_loop_destructor != WYRM_NULL) {
        ctx->primary_main_loop_destructor(ctx->primary_main_loop_mem);
        ctx->primary_main_loop_destructor = WYRM_NULL;
        ctx->primary_main_loop_mem = WYRM_NULL;
        ctx->primary_main_loop = WYRM_NULL;
    }

    // Destroy machine
    if (ctx->primary_state.machine != WYRM_NULL) {
        wyrm_machine_finalize_f(&ctx->machine);
        ctx->primary_state.machine = WYRM_NULL;
    }

    // Destroy Context
    wyrm_allocator_free(allocator, ctx);

    // Destroy the allocator
    if (alloc_destructor) {
        alloc_destructor(alloc_mem);
    }
}
