#ifndef WYRM_API_IMPL_H_
#define WYRM_API_IMPL_H_

#include <wyrm/types.h>
#include <wyrm/internal_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WYRM_OBJECT_LIST_INITIAL_SZ
#define WYRM_OBJECT_LIST_INITIAL_SZ 4
#endif



/* ------------------------------------------------------------------------- */
/* Operations                                                                */
/* ------------------------------------------------------------------------- */

WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs)
{
    WYRM_ASSERT(lhs != WYRM_NULL && rhs != WYRM_NULL);
    if (lhs == rhs) { return true; }
    if (lhs->hash != rhs->hash) { return false; }
    if (lhs->len != rhs->len) { return false; }
    if (lhs->len == 0) { return true; }
    return wyrm_strncmp_f(lhs->str, rhs->str, lhs->len) == 0;
}

WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str)
{
    if (!str) { return 0; }
    return str->hash;
}


WYRM_INLINE void wyrm_string_finalize_f(wyrm_context* context, wyrm_string* self)
{
    wyrm_context_gc_free(context, (void*) self->str);
    self->str = WYRM_NULL;
    self->len = 0;
    self->hash = 0;
}

/* ------------------------------------------------------------------------- */
/* Main Loop API                                                             */
/* ------------------------------------------------------------------------- */

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
