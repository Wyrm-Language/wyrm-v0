#ifndef WYRM_PLATFORM_GLIB_MAINLOOP_H_
#define WYRM_PLATFORM_GLIB_MAINLOOP_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wyrm_main_loop_vt wyrm_glib_mainloop_vt_;

/**
 * Constructor - create new mainloop
 */
wyrm_main_loop *wyrm_glib_mainloop_new(wyrm_allocator *alloc);

/**
 * Destroy main loop
 */
wyrm_error wyrm_glib_mainloop_destroy(wyrm_main_loop* ref);

/**
 * Get total active sources
 */
wyrm_uword wyrm_glib_mainloop_get_active_sources(wyrm_main_loop* ref);

#ifdef __cplusplus
}
#endif

#endif
