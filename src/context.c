#include <wyrm.h>
#include <wyrm/internal_api.h>

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


wyrm_error wyrm_context_init_s(wyrm_context* self, wyrm_main_loop* loop)
{
    wyrm_error last_error = WYRM_ERR_NONE;

    self->parent = WYRM_NULL;
    self->current_fiber = WYRM_NULL;
    self->main_loop = loop;
    self->wakeable_source = wyrm_primitive_null();
    self->wakeable_source_ready = false;

    /* SCAFFOLDING */
    self->first = WYRM_NULL;
    self->last = WYRM_NULL;

    last_error = wyrm_main_loop_add_wakeable(
        loop,
        &self->wakeable_source,
        WYRM_PRIORITY_DEFAULT,
        context_triggered,
        wyrm_primitive_ptr(self));
    if (last_error != WYRM_ERR_NONE) { goto cleanup_end; }
    self->wakeable_source_ready = true;

    return WYRM_ERR_NONE;

cleanup_end:
    self->parent = WYRM_NULL;
    self->current_fiber = WYRM_NULL;
    self->main_loop = WYRM_NULL;
    self->wakeable_source = wyrm_primitive_null();
    return last_error;
}


void wyrm_context_finalize_f(wyrm_context* self)
{
    wyrm_error last_error = WYRM_ERR_NONE;
    if (self == WYRM_NULL) { return; }

    /* Free the wakeable source */
    if (self->wakeable_source_ready) {
        last_error = wyrm_main_loop_remove(self->main_loop, self->wakeable_source);
        WYRM_ASSERT(last_error == WYRM_ERR_NONE);
    }

    /* Free all objects */
    wyrm_object* next = self->first;
    for (wyrm_object* cur = next; cur; cur = next) {
        next = cur->next;
        wyrm_object_finalize_f(self, cur);
        wyrm_context_gc_free(self, cur);
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
        self->main_loop != WYRM_NULL &&
        self->wakeable_source_ready)
    {
        last_error = wyrm_main_loop_trigger(self->main_loop, self->wakeable_source);
    } else {
        last_error = WYRM_ERR_INVAL;
    }
    return last_error;
}



/* SCAFFOLDING - REMOVE ME */

void* wyrm_context_gc_alloc(wyrm_context* context, wyrm_uword dsize)
{
    wyrm_machine* machine = wyrm_context_get_machine(context);
    if (machine == WYRM_NULL) { return WYRM_NULL; }

    return wyrm_allocator_alloc(machine->allocator, dsize);
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
    if (!context || !gc_info) { return; }
    if (!context->first) {
        context->first = gc_info;
        context->last = gc_info;
    } else {
        WYRM_ASSERT(context->last != WYRM_NULL);
        context->last->next = gc_info;
        context->last = gc_info;
    }
}



void wyrm_context_object_init_header_f(wyrm_context* context, wyrm_object* object, const wyrm_object_type* dtype)
{
    WYRM_ASSERT(context != WYRM_NULL && object != WYRM_NULL && dtype != WYRM_NULL);
    wyrm_object_init_header_s(object, dtype);
    wyrm_context_push_gc(context, object);
}


void wyrm_context_gc_start_mark(wyrm_context* context)
{
    if (context == WYRM_NULL) { return; }
    wyrm_object* gc_cur = context->first;
    while (gc_cur) {
        gc_cur->flags &= ~( (wyrm_uword) WYRM_GC_FLAG_MARKED );
        gc_cur = gc_cur->next;
    }
}

void wyrm_context_gc_sweep_f(wyrm_context* context)
{
    WYRM_ASSERT(context != WYRM_NULL && context->parent != WYRM_NULL);
    wyrm_machine* machine = wyrm_context_get_machine(context);
    wyrm_object* first = WYRM_NULL;
    wyrm_object* last = WYRM_NULL;
    wyrm_object* gc_cur = context->first;

    while (gc_cur) {
        wyrm_object* next_gc = gc_cur->next;
        if ((gc_cur->flags & WYRM_GC_FLAG_MARKED) != 0) {
            last = gc_cur;
            if (first == WYRM_NULL) { first = gc_cur; }
        } else {
            if ((gc_cur->flags & WYRM_GC_STATIC) == 0) {
                wyrm_object_finalize_f(context, gc_cur);
                wyrm_allocator_free(machine->allocator, gc_cur);
            }
        }
        gc_cur = next_gc;
    }

    context->first = first;
    context->last = last;
}
