#ifndef WYRM_MAIN_LOOP_H_
#define WYRM_MAIN_LOOP_H_

#include <wyrm/fwd.h>
#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>
#include <wyrm/primitive.h>

WY_BEGIN_DECLS

/**
 * I/O condition flags for file descriptor events
 */
enum wy_io_flag
{
    WY_IO_IN = 1,
    WY_IO_PRI = 2,
    WY_IO_OUT = 4,
    WY_IO_ERR = 8,
    WY_IO_HUP = 16,
    WY_IO_NVAL = 32,
};

/**
 * I/O condition type.
 *
 * A combination of wy_io_flag values bitwise OR'd together to indicate the
 * reason for IO handling entry.
 */
typedef wy_uword wy_io_condition;

/**
 * @brief Main loop source priority abstraction
 *
 * Priority values are backend-agnostic and intentionally limited to a small
 * portable set.
 */
enum wy_priority
{
    WY_PRIORITY_HIGH,
    WY_PRIORITY_DEFAULT,
    WY_PRIORITY_IDLE,
};

typedef enum wy_priority wy_priority;

/**
 * Source callback for idle and timer events
 *
 * This registered callback is invoked according to the registered event type.
 * The `user_data` primitive is registered with the main loop and will be
 * treated as a purely opaque value. If using object reference or memory,
 * then the memory _MUST_ be referenced / managed externally and kept
 * for the lifespan of the callback.
 */
typedef bool (*wy_source_cb)(wy_primitive user_data);

/**
 * Source callback for file descriptor events
 */
typedef bool (*wy_source_handle_cb)(wy_handle handle, wy_io_condition condition, wy_primitive user_data);


/**
 * Main loop virtual table
 */
typedef struct wy_main_loop_vt
{
    wy_error (*add_fd)(wy_main_loop* ref, wy_primitive *out, wy_handle fd, wy_io_condition events, wy_priority priority, wy_source_handle_cb cb, wy_primitive ud);
    wy_error (*add_timer)(wy_main_loop* self, wy_primitive *out, uint32_t ms, wy_priority priority, wy_source_cb cb, wy_primitive ud);
    wy_error (*add_idle)(wy_main_loop* self, wy_primitive *out, wy_source_cb cb, wy_primitive ud);
    wy_error (*add_wakeable)(wy_main_loop* self, wy_primitive *out, wy_priority priority, wy_source_cb cb, wy_primitive ud);
    wy_error (*trigger)(wy_main_loop* self, wy_primitive src);
    wy_error (*remove)(wy_main_loop* self, wy_primitive src);
    wy_error (*iterate)(wy_main_loop* self, bool may_block);
    wy_error (*run)(wy_main_loop* self);
    wy_error (*quit)(wy_main_loop* self);
} wy_main_loop_vt;

/**
 * Main Loop Abstraction
 */
typedef struct wy_main_loop
{
    const wy_main_loop_vt *vt;
} wy_main_loop;


WY_INLINE wy_error wy_main_loop_add_fd(wy_main_loop* ref, wy_primitive *out, wy_handle fd, wy_io_condition events, wy_priority priority, wy_source_handle_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_add_timer(wy_main_loop* self, wy_primitive *out, uint32_t ms, wy_priority priority, wy_source_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_add_idle(wy_main_loop* self, wy_primitive *out, wy_source_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_add_wakeable(wy_main_loop* self, wy_primitive *out, wy_priority priority, wy_source_cb cb, wy_primitive ud);
WY_INLINE wy_error wy_main_loop_trigger(wy_main_loop* self, wy_primitive src);
WY_INLINE wy_error wy_main_loop_remove(wy_main_loop* self, wy_primitive src);
WY_INLINE wy_error wy_main_loop_iterate(wy_main_loop* self, bool may_block);
WY_INLINE wy_error wy_main_loop_run(wy_main_loop* self);
WY_INLINE wy_error wy_main_loop_quit(wy_main_loop* self);


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
