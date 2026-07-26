#ifndef WYRM_INTERNAL_API_H_
#define WYRM_INTERNAL_API_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the next array size given current capacity and initial capacity
 */
WYRM_INLINE wyrm_uword wyrm_next_array_capacity(wyrm_uword current_capacity, wyrm_uword initial)
{
    if (current_capacity >= WYRM_UWORD_HALF) return WYRM_UWORD_MAX;
    return current_capacity == 0 ? initial : current_capacity * 2;
}

#ifdef __cplusplus
}
#endif

#endif
