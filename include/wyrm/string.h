#ifndef WYRM_STRING_H_
#define WYRM_STRING_H_

#include <wyrm/core.h>
#include <wyrm/types.h>
#include <wyrm/object.h>
#include <wyrm/sys/string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wyrm_object_type wyrm_string_type;

struct wyrm_string
{
    wyrm_object object;
    const char* str;
    wyrm_uword len;
    wyrm_uword hash;
};

wyrm_error wyrm_string_new(wyrm_context* machine, const char* src, wyrm_uword len, wyrm_string** out_str);
wyrm_error wyrm_string_strdup(wyrm_context* machine, const char* src, wyrm_string** out_str);

WYRM_INLINE bool wyrm_string_eq_f(wyrm_string* lhs, wyrm_string* rhs);
WYRM_INLINE wyrm_uword wyrm_string_hash_f(wyrm_string* str);


WYRM_INLINE wyrm_uword wyrm_hash_buffer(const char* start, const char* end)
{
    wyrm_uword hash = 0;
    for (const char* cur = start; cur != end; ++cur) {
        hash = hash + (wyrm_uword)(*cur);
    }
    return hash;
}


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
