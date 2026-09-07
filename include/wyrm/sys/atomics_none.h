#ifndef WYRM_SYS_ATOMICS_NONE_H_
#define WYRM_SYS_ATOMICS_NONE_H_

#include <wyrm/sys/atomics.h>

#if defined(WY_ATOMICS_USE_NONE) && WY_ATOMICS_USE_NONE

typedef wy_word wy_atomic_word;

static inline void wy_ref(wy_atomic_word* v)
{
    *v += 1;
}

static inline bool wy_deref(wy_atomic_word* v)
{
    *v -= 1;
    return *v == 0;
}

#endif
#endif
