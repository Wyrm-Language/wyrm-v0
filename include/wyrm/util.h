#ifndef WYRM_UTIL_H_
#define WYRM_UTIL_H_

#include <wyrm/sys/toolchain.h>

// default values recommended by http://isthe.com/chongo/tech/comp/fnv/
#define FNV1A_PRIME  0x01000193
#define FNV1A_SEED   0x811C9DC5

WY_BEGIN_DECLS

WY_INLINE wy_u32 wy_util_bit_ceil_u32(wy_u32 n) {
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
WY_INLINE wy_u64 wy_util_bit_ceil_u64(wy_u64 n) {
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

#if WY_CELL_BITS > 32
#define wy_util_bit_ceil(x) wy_util_bit_ceil_u64((x))
#else
#define wy_util_bit_ceil(x) wy_util_bit_ceil_u32((x))
#endif

WY_INLINE wy_u32 wy_util_fnv1a(wy_u8 b, wy_u32 hash)
{
    return (b ^ hash) * FNV1A_PRIME;
}

WY_INLINE wy_u32 wy_util_fnv1a_init(wy_u8 b)
{
    return wy_util_fnv1a(b, FNV1A_SEED);
}

WY_INLINE wy_u32 wy_util_fnv1a_buffer(void* buffer, wy_uword size)
{
    wy_u32 cur = FNV1A_SEED;
    for (wy_uword i = 0; i < size; ++i) {
        cur = wy_util_fnv1a(((wy_u8*)buffer)[i], cur);
    }
    return cur;
}

WY_INLINE wy_uword wy_util_rehash(wy_uintptr w)
{
    return wy_util_fnv1a_buffer(&w, sizeof(wy_uintptr));
}

/**
 * Get the next array size given current capacity and initial capacity
 */
WY_INLINE wy_uword wy_next_array_capacity(wy_uword current_capacity, wy_uword initial)
{
    if (current_capacity >= WY_UWORD_HALF) return WY_UWORD_MAX;
    return current_capacity == 0 ? initial : current_capacity * 2;
}



WY_END_DECLS

#endif
