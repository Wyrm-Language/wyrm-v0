#ifndef WYRM_SYS_STRING_H_
#define WYRM_SYS_STRING_H_

#include <wyrm/sys/toolchain.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

WY_INLINE void* wy_memcpy(void* dest, const void* src, wy_uword len) {
    return memcpy(dest, src, (size_t) len);
}

WY_INLINE int wy_memcmp(const void* dest, const void* src, wy_uword len)
{
    return memcmp(dest, src, len);
}

WY_INLINE void* wy_memmove(void* dest, const void* src, wy_uword len) {
    return memmove(dest, src, (size_t) len);
}

WY_INLINE void wy_memset(void* buffer, int ch, wy_uword len)
{
    memset(buffer, ch, (size_t) len);
}

WY_INLINE wy_uword wy_strlen_f(const char* value)
{
    return (wy_uword) strlen(value);
}

WY_INLINE void wy_strncpy_f(char* dest, const char* src, wy_uword sz)
{
    (void) memmove(dest, src, (size_t) sz);
}

WY_INLINE int wy_strcmp_f(const char* lh, const char* rh)
{
    return strcmp(lh, rh);
}

WY_INLINE int wy_strncmp_f(const char* lh, const char* rh, size_t len)
{
    return strncmp(lh, rh, len);
}

#ifdef __cplusplus
};
#endif

#endif
