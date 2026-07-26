#ifndef WYRM_INL_STRING_H_
#define WYRM_INL_STRING_H_

#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif

WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs)
{
    WYRM_ASSERT(lhs != WYRM_NULL && rhs != WYRM_NULL);
    if (lhs == rhs) { return true; }
    if (lhs->hash != rhs->hash) { return false; }
    if (lhs->len != rhs->len) { return false; }
    if (lhs->len == 0) { return true; }
    return wyrm_strncmp_f(lhs->str, rhs->str, lhs->len) == 0;
}

WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str)
{
    if (!str) { return 0; }
    return str->hash;
}

#ifdef __cplusplus
}
#endif

#endif
