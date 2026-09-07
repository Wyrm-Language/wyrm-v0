#ifndef WYRM_SYS_TOOLCHAIN_H_
#define WYRM_SYS_TOOLCHAIN_H_

#ifdef __has_include
#if __has_include(<wy_toolchain_config.h>)
#include <wy_toolchain_config.h>
#endif
#endif

#ifdef WY_CONFIG_INCLUDE
#include WY_CONFIG_INCLUDE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stddef.h>
#include <assert.h>
#include <inttypes.h>

#ifndef WY_HAS_STRING_H_
#define WY_HAS_STRING_H_ 1
#endif

#if WY_HAS_STRING_H_
#include <string.h> // memcpy
#endif

// Fix for static_assert missing in old toolchains
#ifndef __cpp_static_assert
#if defined( __STDC_VERSION__ ) && __STDC_VERSION__ < 201112L
#ifndef static_assert
#define static_assert(x, y)
#endif
#elif defined( __STDC_VERSION__ ) && __STDC_VERSION__ < 202000L
#ifndef static_assert
#define static_assert(const_expression, string_literal) _Static_assert(const_expression, string_literal)
#endif
#endif
#endif

#ifndef WY_ASSERT
#if !defined(WY_DISABLE_EXTRA_CHECKS) || !WY_DISABLE_EXTRA_CHECKS
#define WY_ASSERT_CHECKS 1
#define WY_ASSERT(x) assert(x)
#else
#define WY_ASSERT(x)
#endif
#endif

#ifndef WY_INLINE
#ifdef __cplusplus
#define WY_INLINE inline
#else
#define WY_INLINE static inline
#endif
#endif

#define WY_ASSERT_ALWAYS() WY_ASSERT(false)

#define WY_UNUSED(x) (void)(x)

#ifndef WY_UINTPTR_WIDTH
#if UINTPTR_MAX <= 0xFFFFULL
#error Less than 32bit pointer not supported
#elif UINTPTR_MAX <= 0xFFFFFFFFULL
#define WY_UINTPTR_WIDTH 32
#elif UINTPTR_MAX <= 0xFFFFFFFFFFFFFFFFULL
#define WY_UINTPTR_WIDTH 64
#define WY_UINTPTR_WIDTH 64
#else
#error Greater than 64bit pointer not supported
#endif
#endif

#ifndef WY_CELL_BITS
#define WY_CELL_BITS WY_UINTPTR_WIDTH
#endif

#if WY_CELL_BITS == 64

#ifndef WY_PLATFORM_UWORD_DEFINED
#define WY_PLATFORM_UWORD_DEFINED 1
#define WY_PRI_UWORD PRIx64
#define WY_UWORD_MAX UINT64_MAX
#define WY_UWORD_HALF (0x8000000000000000u)
typedef uint64_t wy_uword;
#endif

typedef uint32_t wy_ushort;
typedef int64_t wy_word;
#define WY_PRI_WORD PRId64
#define WY_WORD_MAX INT64_MAX
typedef int32_t wy_short;
typedef float wy_float_s;
typedef double wy_float;
#else
#ifndef WY_PLATFORM_UWORD_DEFINED
typedef uint32_t wy_uword;
#define WY_PLATFORM_UWORD_DEFINED 1
#define WY_UWORD_MAX UINT32_MAX
#define WY_UWORD_HALF (0x80000000u)
#define WY_PRI_UWORD PRIx32
#endif
typedef uint16_t wy_ushort;
typedef int32_t wy_word;
#define WY_PRI_WORD PRId32
#define WY_WORD_MAX INT32_MAX
typedef int16_t wy_short;
typedef float wy_float_s;
typedef float wy_float;
#define WY_FP_IS_SINGLE_PRECISION 1
#endif

typedef uintptr_t wy_uintptr;
typedef wy_word wy_handle;
typedef int64_t wy_i64;
typedef uint64_t wy_u64;
typedef int32_t wy_i32;
typedef uint32_t wy_u32;
typedef uint16_t wy_u16;
typedef uint8_t wy_u8;

#define WY_HAS_U64 1
typedef uint64_t wy_u64;

static_assert(sizeof(wy_uword) >= sizeof(uintptr_t), "uword must store full pointer bits");
static_assert(sizeof(wy_float) <= sizeof(wy_uword), "fp must not exceed size of uword");

#ifndef WY_BYTE_ALIGNMENT
#if WY_CELL_BITS > 32
#define WY_BYTE_ALIGNMENT 8
#define WY_BYTE_ALIGNMENT_BITS 3
#else
#define WY_BYTE_ALIGNMENT 4
#define WY_BYTE_ALIGNMENT_BITS 2
#endif
#endif


#ifndef WY_PLATFORM_PTR_ENCODE_DEFINED

/// @brief Convert C pointer to wyrm data pointer
/// @param src_ptr Pointer with minimum of 32 bit alignment
/// @param bits Bits to encode within the pointer
///
/// Wyrm utilizes allows word aligned pointers to be encoded with bits
/// and then stored within a pointer. Bits may define internal behavior
/// of the pointer. This function is defined for validity within the
/// underlying platform.
static inline wy_uword wy_pointer_encode(const void* src_ptr, wy_uword bits) {
    uintptr_t src_data_uint = (uintptr_t) src_ptr;
    WY_ASSERT((src_data_uint & 0x3) == 0);
    return (src_data_uint & ~0x3ULL) | bits;
}
#define WY_PLATFORM_PTR_ENCODE_DEFINED 1
#endif

#ifndef WY_PLATFORM_PTR_DECODE_DEFINED
#define WY_PLATFORM_PTR_DECODE_DEFINED 1
/// @brief Get pointer encoded in Wyrm Word
/// @param data the Wyrm Word with the included pointer
static inline const void* wy_pointer_decode(wy_uword data) {
    const uintptr_t src_data_uint = data & ~0x3ULL;
    return (const void*) src_data_uint;
}

/// @brief Get bits encoded in Wyrm Word pointer
/// @param data pointer encoded word
/// @return Set bits stored within the encoded pointer
static inline wy_uword wy_pointer_decode_bits(wy_uword data) {
    return data & 0x3;
}

#endif

#ifndef WY_ALIGNED
#define WY_ALIGNED alignas(WY_BYTE_ALIGNMENT)
#endif

#ifndef WY_ASSERT_ALIGNED
#define WY_ALIGNED_MASK ((1 << WY_BYTE_ALIGNMENT_BITS) - 1)
#define WY_ASSERT_ALIGNED(x) WY_ASSERT((((uintptr_t)(x)) & WY_ALIGNED_MASK) == 0)
#endif

#ifndef WY_NULL
#ifdef __cplusplus
#define WY_NULL nullptr
#else
#define WY_NULL NULL
#endif
#endif

/* Invalid Array Index:

   Sentinel used whenever a value returns back an array index and we want
   to have testable validity.
*/
#define WY_IDX_INVALID WY_UWORD_MAX

/* Constraining Array Limits:

   Define the maximum size allowed for internal Wyrm data structures, and the
   maximum array length based on limiting overflow of a wy_uword. This is
   a somewhat arbitrary limit based on current wyrm constants. The maximum
   size of an element stored in an array is 1 << WY_MAX_ARRAY_ENTRY_SZ_BITS
   or 4096 bytes currently; limiting overflow this leaves a maximum of
   ~ 1 million elements on a 32 bit PC.

   Array size and element count could theoretically be tweaked more, but these
   constants provide fast checks without having deal with overflow logic.
*/
#define WY_MAX_ARRAY_ENTRY_SZ_BITS (12)
#define WY_MAX_ARRAY_ENTRY_SZ (1 << WY_MAX_ARRAY_ENTRY_SZ_BITS)
#define WY_MAX_ARRAY_LEN_SZ_BITS (32 - WY_MAX_ARRAY_ENTRY_SZ_BITS)
#define WY_MAX_ARRAY_LEN (1 << WY_MAX_ARRAY_LEN_SZ_BITS)

#ifdef __cplusplus
#define WY_BEGIN_DECLS extern "C" {
#define WY_END_DECLS }
#else
#define WY_BEGIN_DECLS
#define WY_END_DECLS
#endif

#endif
