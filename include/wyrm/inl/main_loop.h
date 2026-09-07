#ifndef WYRM_INL_MAIN_LOOP_INL_H_
#define WYRM_INL_MAIN_LOOP_INL_H_

#include <wyrm/types.h>

WY_BEGIN_DECLS

/**
 * Add fd watcher to the main loop
 */
WY_INLINE wy_error wy_main_loop_add_fd(wy_main_loop* ref, wy_primitive *out, wy_handle fd, wy_io_condition events, wy_priority priority, wy_source_handle_cb cb, wy_primitive ud)
{
    if (ref == WY_NULL || ref->vt == WY_NULL) { return WY_ERR_INVAL; }
    return ref->vt->add_fd(ref, out, fd, events, priority, cb, ud);
}

/**
 * Add timer to the main loop
 */
WY_INLINE wy_error wy_main_loop_add_timer(wy_main_loop* self, wy_primitive *out, uint32_t ms, wy_priority priority, wy_source_cb cb, wy_primitive ud)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->add_timer(self, out, ms, priority, cb, ud);
}

/**
 * Add idle handler to the main loop
 */
WY_INLINE wy_error wy_main_loop_add_idle(wy_main_loop* self, wy_primitive *out, wy_source_cb cb, wy_primitive ud)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->add_idle(self, out, cb, ud);
}

/**
 * Add wakeable handler to the main loop
 */
WY_INLINE wy_error wy_main_loop_add_wakeable(wy_main_loop* self, wy_primitive *out, wy_priority priority, wy_source_cb cb, wy_primitive ud)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->add_wakeable(self, out, priority, cb, ud);
}

/**
 * Add triggerable to the main loop
 */
WY_INLINE wy_error wy_main_loop_trigger(wy_main_loop* self, wy_primitive src)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->trigger(self, src);
}

/**
 * Remove source from main loop
 */
WY_INLINE wy_error wy_main_loop_remove(wy_main_loop* self, wy_primitive src)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->remove(self, src);
}

/**
 * Request iteration of main loop
 */
WY_INLINE wy_error wy_main_loop_iterate(wy_main_loop* self, bool may_block)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->iterate(self, may_block);
}

/**
 * Run the main loop
 */
WY_INLINE wy_error wy_main_loop_run(wy_main_loop* self)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->run(self);
}

/**
 * Quit the main loop
 */
WY_INLINE wy_error wy_main_loop_quit(wy_main_loop* self)
{
    if (self == WY_NULL || self->vt == WY_NULL) { return WY_ERR_INVAL; }
    return self->vt->quit(self);
}

WY_END_DECLS

#endif
