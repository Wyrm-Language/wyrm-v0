#ifndef WYRM_SYS_THREAD_ID_H_
#define WYRM_SYS_THREAD_ID_H_

#include <wyrm/sys/thread_common.h>
#include <wyrm/sys/toolchain.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief OS thread identifier — opaque; defined by the active platform backend
struct wyrm_sys_thread_id;

/// @brief Return the identifier of the calling thread.
static inline struct wyrm_sys_thread_id wyrm_sys_get_thread_id(void);

#ifdef __cplusplus
}
#endif

// ---- Platform backend selection ----

#include <wyrm/sys/thread_id_c11.h>
#include <wyrm/sys/thread_id_none.h>

#ifndef __cplusplus
typedef struct wyrm_sys_thread_id wyrm_sys_thread_id;
#endif

#endif
