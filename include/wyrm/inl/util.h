#ifndef WYRM_INTERNAL_API_H_
#define WYRM_INTERNAL_API_H_

#include <wyrm/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the next array size given current capacity and initial capacity
 */
WY_INLINE wy_uword wy_next_array_capacity(wy_uword current_capacity, wy_uword initial)
{
    if (current_capacity >= WY_UWORD_HALF) return WY_UWORD_MAX;
    return current_capacity == 0 ? initial : current_capacity * 2;
}

#ifdef __cplusplus
}
#endif

#endif
