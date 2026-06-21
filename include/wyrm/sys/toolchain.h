#ifndef WYRM_SYS_TOOLCHAIN_H_
#define WYRM_SYS_TOOLCHAIN_H_

#ifdef __has_include
#if __has_include(<wyrm_toolchain_config.h>)
#include <wyrm_toolchain_config.h>
#endif
#endif

#ifdef WYRM_CONFIG_INCLUDE
#include WYRM_CONFIG_INCLUDE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stddef.h>
#include <assert.h>
#include <inttypes.h>

#ifndef WYRM_HAS_STRING_H_
#define WYRM_HAS_STRING_H_ 1
#endif

#if WYRM_HAS_STRING_H_
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

#ifndef WYRM_ASSERT
#if !defined(WYRM_DISABLE_EXTRA_CHECKS) || !WYRM_DISABLE_EXTRA_CHECKS
#define WYRM_ASSERT_CHECKS 1
#define WYRM_ASSERT(x) assert(x)
#else
#define WYRM_ASSERT(x)
#endif
#endif

#define WYRM_ASSERT_ALWAYS() WYRM_ASSERT(false)

#define WYRM_UNUSED(x) (void)(x)

#ifndef WYRM_UINTPTR_WIDTH
#if UINTPTR_MAX <= 0xFFFFULL
#error Less than 32bit pointer not supported
#elif UINTPTR_MAX <= 0xFFFFFFFFULL
#define WYRM_UINTPTR_WIDTH 32
#elif UINTPTR_MAX <= 0xFFFFFFFFFFFFFFFFULL
#define WYRM_UINTPTR_WIDTH 64
#define WYRM_UINTPTR_WIDTH 64
#else
#error Greater than 64bit pointer not supported
#endif
#endif

#ifndef WYRM_CELL_BITS
#define WYRM_CELL_BITS WYRM_UINTPTR_WIDTH
#endif

#if WYRM_CELL_BITS == 64

#ifndef WYRM_PLATFORM_UWORD_DEFINED
#define WYRM_PLATFORM_UWORD_DEFINED 1
#define WYRM_PRI_UWORD PRIx64
#define WYRM_UWORD_MAX UINT64_MAX
typedef uint64_t wyrm_uword;
#endif

typedef uint32_t wyrm_ushort;
typedef int64_t wyrm_word;
#define WYRM_PRI_WORD PRId64
#define WYRM_WORD_MAX INT64_MAX
typedef int32_t wyrm_short;
typedef float wyrm_float_s;
typedef double wyrm_float;
#else
#ifndef WYRM_PLATFORM_UWORD_DEFINED
typedef uint32_t wyrm_uword;
#define WYRM_PLATFORM_UWORD_DEFINED 1
#define WYRM_UWORD_MAX UINT32_MAX
#define WYRM_PRI_UWORD PRIx32
#endif
typedef uint16_t wyrm_ushort;
typedef int32_t wyrm_word;
#define WYRM_PRI_WORD PRId32
#define WYRM_WORD_MAX INT32_MAX
typedef int16_t wyrm_short;
typedef float wyrm_float_s;
typedef float wyrm_float;
#define WYRM_FP_IS_SINGLE_PRECISION 1
#endif

typedef uintptr_t wyrm_uintptr;
typedef wyrm_word wyrm_handle;

static_assert(sizeof(wyrm_uword) >= sizeof(uintptr_t), "uword must store full pointer bits");
static_assert(sizeof(wyrm_float) <= sizeof(wyrm_uword), "fp must not exceed size of uword");


#ifndef WYRM_BYTE_ALIGNMENT
#if WYRM_CELL_BITS > 32
#define WYRM_BYTE_ALIGNMENT 8
#define WYRM_BYTE_ALIGNMENT_BITS 3
#else
#define WYRM_BYTE_ALIGNMENT 4
#define WYRM_BYTE_ALIGNMENT_BITS 2
#endif
#endif


#ifndef WYRM_PLATFORM_PTR_ENCODE_DEFINED

/// @brief Convert C pointer to wyrm data pointer
/// @param src_ptr Pointer with minimum of 32 bit alignment
/// @param bits Bits to encode within the pointer
///
/// Wyrm utilizes allows word aligned pointers to be encoded with bits
/// and then stored within a pointer. Bits may define internal behavior
/// of the pointer. This function is defined for validity within the
/// underlying platform.
static inline wyrm_uword wyrm_pointer_encode(const void* src_ptr, wyrm_uword bits) {
    uintptr_t src_data_uint = (uintptr_t) src_ptr;
    WYRM_ASSERT((src_data_uint & 0x3) == 0);
    return (src_data_uint & ~0x3ULL) | bits;
}
#define WYRM_PLATFORM_PTR_ENCODE_DEFINED 1
#endif

#ifndef WYRM_PLATFORM_PTR_DECODE_DEFINED
#define WYRM_PLATFORM_PTR_DECODE_DEFINED 1
/// @brief Get pointer encoded in Wyrm Word
/// @param data the Wyrm Word with the included pointer
static inline const void* wyrm_pointer_decode(wyrm_uword data) {
    const uintptr_t src_data_uint = data & ~0x3ULL;
    return (const void*) src_data_uint;
}

/// @brief Get bits encoded in Wyrm Word pointer
/// @param data pointer encoded word
/// @return Set bits stored within the encoded pointer
static inline wyrm_uword wyrm_pointer_decode_bits(wyrm_uword data) {
    return data & 0x3;
}

#endif

#ifndef WYRM_ALIGNED
#define WYRM_ALIGNED alignas(WYRM_BYTE_ALIGNMENT)
#endif

#ifndef WYRM_ASSERT_ALIGNED
#define WYRM_ALIGNED_MASK ((1 << WYRM_BYTE_ALIGNMENT_BITS) - 1)
#define WYRM_ASSERT_ALIGNED(x) WYRM_ASSERT((((uintptr_t)(x)) & WYRM_ALIGNED_MASK) == 0)
#endif

#ifndef WYRM_NULL
#ifdef __cplusplus
#define WYRM_NULL nullptr
#else
#define WYRM_NULL NULL
#endif
#endif

#define WYRM_IDX_INVALID WYRM_UWORD_MAX

#endif
