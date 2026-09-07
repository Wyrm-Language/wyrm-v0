#ifndef WYRM_VALUE_H_
#define WYRM_VALUE_H_

#include <wyrm/fwd.h>
#include <wyrm/primitive.h>

WY_BEGIN_DECLS

/**
 * @brief A typed primitive
 */
struct wy_value
{
    wy_type_tag type;
    wy_primitive data;
};

enum {
    WY_PRIMITIVE_SIZE = sizeof(wy_primitive)
};

static_assert(WY_PRIMITIVE_SIZE >= sizeof(uintptr_t), "Primitive must allow storage of a pointer");

/**
 * @brief Test whether a value holds a traversable object reference
 *
 * @param value Value to test
 * @return true when the value references an allocated object
 */
WY_INLINE bool wy_value_is_gc_ref_f(wy_value value)
{
    return wy_type_is_object(value.type) && value.data.gc_object != WY_NULL;
}


/**
 * @brief Create a word primitive value
 */
WY_INLINE wy_value wy_value_word(wy_word value)
{
    wy_value v = { .type = WY_TYPE_TAG_WORD, .data = wy_primitive_int(value) };
    return v;
}

/**
 * @brief Create a word primitive value
 */
WY_INLINE wy_value wy_value_uword(wy_uword value)
{
    wy_value v = { .type = WY_TYPE_TAG_WORD, .data = wy_primitive_uword(value) };
    return v;
}


/**
 * @brief Create a null primitive value
 */
WY_INLINE wy_primitive wy_primitive_null(void)
{
    wy_primitive v = { .gc_object = WY_NULL };
    return v;
}

/**
 * @brief Create 'nil' primitive
 */
WY_INLINE wy_value wy_value_nil(void)
{
    wy_value v = {
        .type = WY_TYPE_TAG_NIL,
        .data = wy_primitive_null()
    };
    return v;
}

/**
 * @brief Create Unset primitive
 */
WY_INLINE wy_value wy_value_unset(void)
{
    wy_value v = {
        .type = WY_TYPE_TAG_ERROR,
        .data = wy_primitive_null()
    };
    return v;
}


WY_END_DECLS

#endif
