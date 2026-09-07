#ifndef WYRM_PLATFORM_GLIB_MAINLOOP_H_
#define WYRM_PLATFORM_GLIB_MAINLOOP_H_

#include <wyrm/main_loop.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wy_main_loop_vt wy_glib_mainloop_vt_;

/**
 * Constructor - create new mainloop
 */
wy_main_loop *wy_glib_mainloop_new(wy_allocator *alloc);

/**
 * Destroy main loop
 */
wy_error wy_glib_mainloop_destroy(wy_main_loop* ref);

/**
 * Get total active sources
 */
wy_uword wy_glib_mainloop_get_active_sources(wy_main_loop* ref);

#ifdef __cplusplus
}
#endif

#endif
