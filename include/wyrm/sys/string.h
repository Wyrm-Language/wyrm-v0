#ifndef WYRM_SYS_STRING_H_
#define WYRM_SYS_STRING_H_

#include <wyrm/sys/toolchain.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

WYRM_INLINE void* wyrm_memcpy(void* dest, const void* src, wyrm_uword len) {
    return memcpy(dest, src, (size_t) len);
}

WYRM_INLINE int wyrm_memcmp(const void* dest, const void* src, wyrm_uword len)
{
    return memcmp(dest, src, len);
}

WYRM_INLINE void* wyrm_memmove(void* dest, const void* src, wyrm_uword len) {
    return memmove(dest, src, (size_t) len);
}

WYRM_INLINE void wyrm_memset(void* buffer, int ch, wyrm_uword len)
{
    memset(buffer, ch, (size_t) len);
}

WYRM_INLINE wyrm_uword wyrm_strlen_f(const char* value)
{
    return (wyrm_uword) strlen(value);
}

WYRM_INLINE void wyrm_strncpy_f(char* dest, const char* src, wyrm_uword sz)
{
    (void) memmove(dest, src, (size_t) sz);
}

WYRM_INLINE int wyrm_strcmp_f(const char* lh, const char* rh)
{
    return strcmp(lh, rh);
}

WYRM_INLINE int wyrm_strncmp_f(const char* lh, const char* rh, size_t len)
{
    return strncmp(lh, rh, len);
}

#ifdef __cplusplus
};
#endif

#endif
