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
 * @param ud the wyrm_context programmed into the main loop directly
 * @return Always returns true; this trigger is active until context
 *         itself is destroyed.
 */
static bool context_triggered(wyrm_primitive ud)
{
    wyrm_context* self = WYRM_PRIMITIVE_PTR(wyrm_context, ud);
    if (self->current_fiber != WYRM_NULL) {
        wyrm_error last_error = wyrm_fiber_exec_f(self->current_fiber);
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

    last_error = wyrm_main_loop_add_wakeable(
        loop,
        &self->wakeable_source,
        WYRM_PRIORITY_DEFAULT,
        context_triggered,
        wyrm_primitive_ptr(self));
    if (last_error != WYRM_ERR_NONE) { goto cleanup_end; }

    return WYRM_ERR_NONE;

cleanup_end:
    self->parent = WYRM_NULL;
    self->current_fiber = WYRM_NULL;
    self->main_loop = WYRM_NULL;
    self->wakeable_source = wyrm_primitive_null();
    return last_error;
}


wyrm_error wyrm_context_attach_fiber(wyrm_context* self, wyrm_fiber* fiber)
{
    if (self == WYRM_NULL || fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (fiber->parent != WYRM_NULL) { return WYRM_ERR_BUSY; }
    self->current_fiber = fiber;
    fiber->parent = self;
    return WYRM_ERR_NONE;
}
