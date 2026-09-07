#ifndef WYRM_SYS_ATOMICS_H_
#define WYRM_SYS_ATOMICS_H_

#include <wyrm/sys/toolchain.h>

#ifndef WY_ATOMICS_IMPL

#if defined(__has_builtin) && __has_builtin(__atomic_fetch_add)
#  define WY_ATOMICS_USE_BUILTIN 1
#  define WY_ATOMICS_IMPL "BUILTIN"
#elif defined(__GCC_ATOMIC_INT_LOCK_FREE) && __GCC_ATOMIC_INT_LOCK_FREE > 0
#  define WY_ATOMICS_USE_BUILTIN 1
#  define WY_ATOMICS_IMPL "BUILTIN"
#endif

#ifndef WY_ATOMICS_IMPL
#  define WY_ATOMICS_USE_NONE 1
#  define WY_ATOMICS_IMPL "NONE"
#endif

#endif

#include <wyrm/sys/atomics_none.h>
#include <wyrm/sys/atomics_c11.h>

#endif
