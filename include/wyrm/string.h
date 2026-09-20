#ifndef WYRM_STRING_H_
#define WYRM_STRING_H_

#include <wyrm/object.h>
#include <wyrm/sys/string.h>
#include <wyrm/util.h>

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

/** Concatenate `lhs` and `rhs` into a freshly allocated string. */
wy_error wy_string_concat(wy_context* context, wy_string* lhs, wy_string* rhs, wy_string** out_str);

WY_INLINE bool wy_string_eq_f(wy_string* lhs, wy_string* rhs);
WY_INLINE wy_uword wy_string_hash_f(wy_string* str);
WY_INLINE int wy_string_cmp_f(wy_string* lhs, wy_string* rhs);


WY_INLINE wy_uword wy_hash_buffer(const char* start, const char* end)
{
    return (wy_uword) wy_util_fnv1a_buffer((void*) start, (wy_uword)(end - start));
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

/** Byte-lexicographic three-way compare, ordering the shorter prefix first. */
WY_INLINE int wy_string_cmp_f(wy_string* lhs, wy_string* rhs)
{
    WY_ASSERT(lhs != WY_NULL && rhs != WY_NULL);
    if (lhs == rhs) { return 0; }
    wy_uword n = lhs->len < rhs->len ? lhs->len : rhs->len;
    int c = (n == 0) ? 0 : wy_strncmp_f(lhs->str, rhs->str, n);
    if (c != 0) { return c; }
    if (lhs->len < rhs->len) { return -1; }
    if (lhs->len > rhs->len) { return 1; }
    return 0;
}


#ifdef __cplusplus
}
#endif

#endif
