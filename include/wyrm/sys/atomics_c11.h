#ifndef WYRM_SYS_ATOMICS_C11_H_
#define WYRM_SYS_ATOMICS_C11_H_

#include <wyrm/sys/atomics.h>

#if defined(WY_ATOMICS_USE_BUILTIN) && WY_ATOMICS_USE_BUILTIN

typedef int wy_atomic_word;

static inline void wy_ref(wy_atomic_word* v)
{
    __atomic_fetch_add(v, 1, __ATOMIC_RELAXED);
}

static inline bool wy_deref(wy_atomic_word* v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_ACQ_REL) == 1;
}

#endif
#endif
