#ifndef WYRM_SYS_ATOMICS_NONE_H_
#define WYRM_SYS_ATOMICS_NONE_H_

#include <wyrm/sys/atomics.h>

#if defined(WYRM_ATOMICS_USE_NONE) && WYRM_ATOMICS_USE_NONE

typedef wyrm_word wyrm_atomic_word;

static inline void wyrm_ref(wyrm_atomic_word* v)
{
    *v += 1;
}

static inline bool wyrm_deref(wyrm_atomic_word* v)
{
    *v -= 1;
    return *v == 0;
}

#endif
#endif
