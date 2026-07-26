#ifndef WYRM_INL_MAIN_LOOP_INL_H_
#define WYRM_INL_MAIN_LOOP_INL_H_

#include <wyrm/inl/stack.h>
#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Add fd watcher to the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_add_fd(wyrm_main_loop* ref, wyrm_primitive *out, wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud)
{
    if (ref == WYRM_NULL || ref->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return ref->vt->add_fd(ref, out, fd, events, priority, cb, ud);
}

/**
 * Add timer to the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_add_timer(wyrm_main_loop* self, wyrm_primitive *out, uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_timer(self, out, ms, priority, cb, ud);
}

/**
 * Add idle handler to the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_add_idle(wyrm_main_loop* self, wyrm_primitive *out, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_idle(self, out, cb, ud);
}

/**
 * Add wakeable handler to the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_add_wakeable(wyrm_main_loop* self, wyrm_primitive *out, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_wakeable(self, out, priority, cb, ud);
}

/**
 * Add triggerable to the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_trigger(wyrm_main_loop* self, wyrm_primitive src)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->trigger(self, src);
}

/**
 * Remove source from main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_remove(wyrm_main_loop* self, wyrm_primitive src)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->remove(self, src);
}

/**
 * Request iteration of main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_iterate(wyrm_main_loop* self, bool may_block)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->iterate(self, may_block);
}

/**
 * Run the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_run(wyrm_main_loop* self)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->run(self);
}

/**
 * Quit the main loop
 */
WYRM_INLINE wyrm_error wyrm_main_loop_quit(wyrm_main_loop* self)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->quit(self);
}

#ifdef __cplusplus
}
#endif

#endif
