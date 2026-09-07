#ifndef WYRM_STRING_H_
#define WYRM_STRING_H_

#include <wyrm/object.h>
#include <wyrm/sys/string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const wy_object_type wy_string_type;

struct wy_string
{
    wy_object object;
    const char* str;
    wy_uword len;
    wy_uword hash;
};

wy_error wy_string_new(wy_context* machine, const char* src, wy_uword len, wy_string** out_str);
wy_error wy_string_strdup(wy_context* machine, const char* src, wy_string** out_str);

WY_INLINE bool wy_string_eq_f(wy_string* lhs, wy_string* rhs);
WY_INLINE wy_uword wy_string_hash_f(wy_string* str);


WY_INLINE wy_uword wy_hash_buffer(const char* start, const char* end)
{
    wy_uword hash = 0;
    for (const char* cur = start; cur != end; ++cur) {
        hash = hash + (wy_uword)(*cur);
    }
    return hash;
}


WY_INLINE bool wy_string_eq_f(wy_string* lhs, wy_string* rhs)
{
    WY_ASSERT(lhs != WY_NULL && rhs != WY_NULL);
    if (lhs == rhs) { return true; }
    if (lhs->hash != rhs->hash) { return false; }
    if (lhs->len != rhs->len) { return false; }
    if (lhs->len == 0) { return true; }
    return wy_strncmp_f(lhs->str, rhs->str, lhs->len) == 0;
}

WY_INLINE wy_uword wy_string_hash_f(wy_string* str)
{
    if (!str) { return 0; }
    return str->hash;
}


#ifdef __cplusplus
}
#endif

#endif
