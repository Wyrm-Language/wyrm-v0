#include <wyrm.h>
#include <wyrm/mem_info.h>

/**
 * One or more fibers flagged active and ready.
 *
 * This is triggered when a fiber is ready to be executed. It iterates
 * once on all runnable fibers before returning to the main loop.
 *
 * TODO: We don't currently handle the situation where a fiber isn't
 *       operating correctly. This should wakeup a management thread
 *       and/or flag the status on the context.
 *
 * TODO: We just hardcode current_fiber in the context, instead -
 *       we should have some scheduling/extended logic here to
 *       determine which fiber needs to run. Currently, only
 *       1 fiber, so not an issue.
 *
 * @param ud the wyrm_context programmed into the main loop directly
 * @return Always returns true; this trigger is active until context
 *         itself is destroyed.
 */
static bool context_triggered(wyrm_primitive ud)
{
    /* Grab current context */
    wyrm_context* self = WYRM_PRIMITIVE_PTR(wyrm_context, ud);

    /* State for operations */
    wyrm_state state;
    wyrm_state_init_context_f(&state, self);

    /* TODO: determine correct fiber to execute, set state->current_fiber appropriately */
    state.fiber = self->current_fiber;

    if (state.fiber != WYRM_NULL) {
        wyrm_error last_error = wyrm_state_exec(&state);
        if (last_error != WYRM_ERR_NONE) {
            /* TODO: flag/update context and fiber */
        }
    }
    return true;
}


void wyrm_context_init_s(wyrm_context* self)
{
    self->parent = WYRM_NULL;
    self->root_module = WYRM_NULL;
    self->current_fiber = WYRM_NULL;
    self->main_loop = WYRM_NULL;
    self->wakeable_source = wyrm_primitive_null();

    wyrm_gc_init_f(&self->arena, WYRM_NULL);
}


void wyrm_context_finalize_f(wyrm_context* self)
{
    if (self == WYRM_NULL) { return; }

    /* Free the wakeable source */
    if (self->main_loop != WYRM_NULL) {
        wyrm_context_detach_loop(self);
    }

    /* Free all objects */
    wyrm_gc_finalize_f(self, &self->arena);
}

/**
 * Set the root module associated with the context.
 *
 * A context "runs" via association with a root module. This module is the
 * 'main' entry point for the context. After the module has executed, the
 * context may remain - allowing asynchronous events keyed off an associated
 * main loop if constructed.
 *
 * @param context The context receiving the module.
 * @param module The dynamic module state object
 * @return WYRM_ERR_NONE on success; WYRM_ERR_BUSY if root already set.
 */
wyrm_error wy_context_set_root(wyrm_context* context, wy_module* module)
{
    if (context->root_module != WYRM_NULL) { return WYRM_ERR_BUSY; }
    context->root_module = module;
    return WYRM_ERR_NONE;
}

wyrm_error wyrm_context_attach_loop(wyrm_context* self, wyrm_main_loop* loop)
{
    if (self->main_loop != WYRM_NULL) { return WYRM_ERR_BUSY; }

    wyrm_error last_error = wyrm_main_loop_add_wakeable(
        loop,
        &self->wakeable_source,
        WYRM_PRIORITY_DEFAULT,
        context_triggered,
        wyrm_primitive_ptr(self));
    if (last_error == WYRM_ERR_NONE) {
        self->main_loop = loop;
    }

    return last_error;
}

void wyrm_context_detach_loop(wyrm_context* self)
{
    if (self->main_loop) {
        wyrm_main_loop_remove(self->main_loop, self->wakeable_source);
        self->wakeable_source = wyrm_primitive_null();
        self->main_loop = WYRM_NULL;
    }
}


wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber)
{
    if (self == WYRM_NULL || fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (fiber->parent != WYRM_NULL) { return WYRM_ERR_BUSY; }
    self->current_fiber = fiber;
    fiber->parent = self;
    return WYRM_ERR_NONE;
}



wyrm_error wyrm_context_activate(wyrm_context* self, wyrm_fiber* fiber)
{
    wyrm_error last_error = WYRM_ERR_NONE;
    WYRM_UNUSED(fiber);

    if (self != WYRM_NULL &&
        self->main_loop != WYRM_NULL)
    {
        last_error = wyrm_main_loop_trigger(self->main_loop, self->wakeable_source);
    } else {
        last_error = WYRM_ERR_INVAL;
    }
    return last_error;
}


void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize)
{
    wyrm_machine* machine = wyrm_context_get_machine(context);
    if (machine == WYRM_NULL) { return WYRM_NULL; }

    return wyrm_allocator_alloc(machine->allocator, dsize);
}


void* wyrm_context_gc_realloc(wyrm_context* context, void* ptr, wyrm_uword new_size)
{
    wyrm_machine* machine = wyrm_context_get_machine(context);
    if (machine == WYRM_NULL) { return WYRM_NULL; }

    return wyrm_allocator_realloc(machine->allocator, ptr, new_size);
}


void wyrm_context_gc_free(wyrm_context* context, void* ptr)
{
    wyrm_machine* machine = wyrm_context_get_machine(context);
    if (machine != WYRM_NULL) {
        wyrm_allocator_free(machine->allocator, ptr);
    }
}


void wyrm_context_push_gc(wyrm_context* context, wyrm_object* gc_info)
{
    wyrm_gc_track(&context->arena, gc_info);
}


void wyrm_context_object_init_header_f(wyrm_context* context, wyrm_object* object, const wyrm_object_type* dtype)
{
    WYRM_ASSERT(context != WYRM_NULL && object != WYRM_NULL && dtype != WYRM_NULL);
    wyrm_object_init_header_s(object, dtype);
    wyrm_context_push_gc(context, object);
}


void wyrm_context_gc_full_run(wyrm_state* state, wyrm_context* context)
{
    wyrm_gc_collect_start_f(context, &context->arena);

    if (context->current_fiber != WYRM_NULL) {
        wyrm_gc_object_visit(state, (wyrm_object*) context->current_fiber);
    }

    if (context->root_module != WYRM_NULL) {
        wyrm_gc_object_visit(state, (wyrm_object*) context->root_module);
    }

    wyrm_gc_collect_finish_f(context, &context->arena);
}

/**
 * Grow `mem_info` to hold at least `sz` bytes
 */
wyrm_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz)
{
    WYRM_ASSERT(context != WYRM_NULL && mem_info != WYRM_NULL);

    if (wy_mem_info_sz_f(mem_info) >= sz) { return WYRM_ERR_NONE; }
    if (wy_mem_info_is_static_f(mem_info)) { return WYRM_ERR_INVAL; }

    void* begin = wyrm_context_gc_realloc(context, mem_info->begin, sz);
    if (begin == WYRM_NULL) { return WYRM_ERR_NOMEM; }

    mem_info->begin = begin;
    mem_info->end = (void*) ((char*) begin + sz);
    return WYRM_ERR_NONE;
}

/**
 * Release memory held by `mem_info` and reset it to empty
 */
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info)
{
    WYRM_ASSERT(context != WYRM_NULL && mem_info != WYRM_NULL);

    if (!wy_mem_info_is_static_f(mem_info)) {
        wyrm_context_gc_free(context, mem_info->begin);
    }
    wy_mem_info_init_empty_s(mem_info);
}
