#ifndef WYRM_UTIL_H_
#define WYRM_UTIL_H_

#include <wyrm/core.h>

// default values recommended by http://isthe.com/chongo/tech/comp/fnv/
#define FNV1A_PRIME  0x01000193
#define FNV1A_SEED   0x811C9DC5

WYRM_BEGIN_DECLS

WYRM_INLINE wy_u32 wy_util_bit_ceil_u32(wy_u32 n) {
    if (n <= 1) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

#if defined(WY_HAS_U64) && WY_HAS_U64
WYRM_INLINE wy_u64 wy_util_bit_ceil_u64(wy_u64 n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}
#endif

#if WYRM_CELL_BITS > 32
#define wy_util_bit_ceil(x) wy_util_bit_ceil_u64((x))
#else
#define wy_util_bit_ceil(x) wy_util_bit_ceil_u32((x))
#endif

WYRM_INLINE wy_u32 wy_util_fnv1a(wy_u8 b, wy_u32 hash)
{
    return (b ^ hash) * FNV1A_PRIME;
}

WYRM_INLINE wy_u32 wy_util_fnv1a_init(wy_u8 b)
{
    return wy_util_fnv1a(b, FNV1A_SEED);
}

WYRM_INLINE wy_u32 wy_util_fnv1a_buffer(void* buffer, wyrm_uword size)
{
    wy_u32 cur = FNV1A_SEED;
    for (wyrm_uword i = 0; i < size; ++i) {
        cur = wy_util_fnv1a(((wy_u8*)buffer)[i], cur);
    }
    return cur;
}

WYRM_INLINE wy_uword wy_util_rehash(wyrm_uintptr w)
{
    return wy_util_fnv1a_buffer(&w, sizeof(wyrm_uintptr));
}


WYRM_END_DECLS

#endif
