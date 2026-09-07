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
 * @param ud the wy_context programmed into the main loop directly
 * @return Always returns true; this trigger is active until context
 *         itself is destroyed.
 */
static bool context_triggered(wy_primitive ud)
{
    /* Grab current context */
    wy_context* self = WY_PRIMITIVE_PTR(wy_context, ud);

    /* State for operations */
    wy_state state;
    wy_state_init_context_f(&state, self);

    /* TODO: determine correct fiber to execute, set state->current_fiber appropriately */
    state.fiber = self->current_fiber;

    if (state.fiber != WY_NULL) {
        wy_error last_error = wy_state_exec(&state);
        if (last_error != WY_ERR_NONE) {
            /* TODO: flag/update context and fiber */
        }
    }
    return true;
}


void wy_context_init_s(wy_context* self)
{
    self->parent = WY_NULL;
    self->root_module = WY_NULL;
    self->current_fiber = WY_NULL;
    self->main_loop = WY_NULL;
    self->wakeable_source = wy_primitive_null();

    wy_gc_init_f(&self->arena, WY_NULL);
}


void wy_context_finalize_f(wy_context* self)
{
    if (self == WY_NULL) { return; }

    /* Free the wakeable source */
    if (self->main_loop != WY_NULL) {
        wy_context_detach_loop(self);
    }

    /* Free all objects */
    wy_gc_finalize_f(self, &self->arena);
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
 * @return WY_ERR_NONE on success; WY_ERR_BUSY if root already set.
 */
wy_error wy_context_set_root(wy_context* context, wy_module* module)
{
    if (context->root_module != WY_NULL) { return WY_ERR_BUSY; }
    context->root_module = module;
    return WY_ERR_NONE;
}

wy_error wy_context_attach_loop(wy_context* self, wy_main_loop* loop)
{
    if (self->main_loop != WY_NULL) { return WY_ERR_BUSY; }

    wy_error last_error = wy_main_loop_add_wakeable(
        loop,
        &self->wakeable_source,
        WY_PRIORITY_DEFAULT,
        context_triggered,
        wy_primitive_ptr(self));
    if (last_error == WY_ERR_NONE) {
        self->main_loop = loop;
    }

    return last_error;
}

void wy_context_detach_loop(wy_context* self)
{
    if (self->main_loop) {
        wy_main_loop_remove(self->main_loop, self->wakeable_source);
        self->wakeable_source = wy_primitive_null();
        self->main_loop = WY_NULL;
    }
}


wy_error wy_context_attach_fiber(wy_context* self, wy_fiber* fiber)
{
    if (self == WY_NULL || fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (fiber->parent != WY_NULL) { return WY_ERR_BUSY; }
    self->current_fiber = fiber;
    fiber->parent = self;
    return WY_ERR_NONE;
}



wy_error wy_context_activate(wy_context* self, wy_fiber* fiber)
{
    wy_error last_error = WY_ERR_NONE;
    WY_UNUSED(fiber);

    if (self != WY_NULL &&
        self->main_loop != WY_NULL)
    {
        last_error = wy_main_loop_trigger(self->main_loop, self->wakeable_source);
    } else {
        last_error = WY_ERR_INVAL;
    }
    return last_error;
}


void* wy_context_gc_alloc(wy_context* context, wy_uword dsize)
{
    wy_machine* machine = wy_context_get_machine(context);
    if (machine == WY_NULL) { return WY_NULL; }

    return wy_allocator_alloc(machine->allocator, dsize);
}


void* wy_context_gc_realloc(wy_context* context, void* ptr, wy_uword new_size)
{
    wy_machine* machine = wy_context_get_machine(context);
    if (machine == WY_NULL) { return WY_NULL; }

    return wy_allocator_realloc(machine->allocator, ptr, new_size);
}


void wy_context_gc_free(wy_context* context, void* ptr)
{
    wy_machine* machine = wy_context_get_machine(context);
    if (machine != WY_NULL) {
        wy_allocator_free(machine->allocator, ptr);
    }
}


void wy_context_push_gc(wy_context* context, wy_object* gc_info)
{
    wy_gc_track(&context->arena, gc_info);
}


void wy_context_object_init_header_f(wy_context* context, wy_object* object, const wy_object_type* dtype)
{
    WY_ASSERT(context != WY_NULL && object != WY_NULL && dtype != WY_NULL);
    wy_object_init_header_s(object, dtype);
    wy_context_push_gc(context, object);
}


void wy_context_gc_full_run(wy_state* state, wy_context* context)
{
    wy_gc_collect_start_f(context, &context->arena);

    if (context->current_fiber != WY_NULL) {
        wy_gc_object_visit(state, (wy_object*) context->current_fiber);
    }

    if (context->root_module != WY_NULL) {
        wy_gc_object_visit(state, (wy_object*) context->root_module);
    }

    wy_gc_collect_finish_f(context, &context->arena);
}

/**
 * Grow `mem_info` to hold at least `sz` bytes
 */
wy_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz)
{
    WY_ASSERT(context != WY_NULL && mem_info != WY_NULL);

    if (wy_mem_info_sz_f(mem_info) >= sz) { return WY_ERR_NONE; }
    if (wy_mem_info_is_static_f(mem_info)) { return WY_ERR_INVAL; }

    void* begin = wy_context_gc_realloc(context, mem_info->begin, sz);
    if (begin == WY_NULL) { return WY_ERR_NOMEM; }

    mem_info->begin = begin;
    mem_info->end = (void*) ((char*) begin + sz);
    return WY_ERR_NONE;
}

/**
 * Release memory held by `mem_info` and reset it to empty
 */
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info)
{
    WY_ASSERT(context != WY_NULL && mem_info != WY_NULL);

    if (!wy_mem_info_is_static_f(mem_info)) {
        wy_context_gc_free(context, mem_info->begin);
    }
    wy_mem_info_init_empty_s(mem_info);
}
