#ifndef WYRM_SYS_ATOMICS_C11_H_
#define WYRM_SYS_ATOMICS_C11_H_

#include <wyrm/sys/atomics.h>

#if defined(WYRM_ATOMICS_USE_BUILTIN) && WYRM_ATOMICS_USE_BUILTIN

typedef int wyrm_atomic_word;

static inline void wyrm_ref(wyrm_atomic_word* v)
{
    __atomic_fetch_add(v, 1, __ATOMIC_RELAXED);
}

static inline bool wyrm_deref(wyrm_atomic_word* v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_ACQ_REL) == 1;
}

#endif
#endif
