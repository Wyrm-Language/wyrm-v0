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
 * @brief Create an unsigned word primitive value
 */
WY_INLINE wy_value wy_value_uword(wy_uword value)
{
    wy_value v = { .type = WY_TYPE_TAG_UWORD, .data = wy_primitive_uword(value) };
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
 * @brief Create a float primitive value
 */
WY_INLINE wy_value wy_value_float(wy_float value)
{
    wy_value v = { .type = WY_TYPE_TAG_FLOAT, .data = { .fp = value } };
    return v;
}

/**
 * @brief Create a bool primitive value
 */
WY_INLINE wy_value wy_value_bool(bool value)
{
    wy_value v = { .type = WY_TYPE_TAG_BOOL, .data = { .flag = value } };
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

/**
 * @brief Create a symbol primitive value
 */
WY_INLINE wy_value wy_value_symbol(wy_symbol value)
{
    wy_value v = { .type = WY_TYPE_TAG_SYMBOL, .data = { .symtab_entry = value } };
    return v;
}

/**
 * @brief Create a primitive-type value naming `tag`
 */
WY_INLINE wy_value wy_value_ptype(wy_type_tag tag)
{
    wy_value v = { .type = WY_TYPE_TAG_PTYPE, .data = wy_primitive_uword((wy_uword) tag) };
    return v;
}

/**
 * @brief Wrap a heap object pointer as a value of the given tag
 *
 * `tag` must be at or past WY_TYPE_TAG_GC_PATH_START (an object-bearing tag).
 */
WY_INLINE wy_value wy_value_object(wy_type_tag tag, wy_object* object)
{
    WY_ASSERT(wy_type_is_object(tag));
    wy_value v = { .type = tag, .data = { .gc_object = object } };
    return v;
}

/**
 * @brief Test whether a value is Unset or a live error
 *
 * Matches pypoc/wypoc/wyrm_builtins.py's `is_error`: true for a realised
 * `wy_error_obj*` (non-NULL ERROR) *and* for Unset itself (`{ERROR, NULL}`)
 * - this is what `jerr`/`jnerr` and `?=` (jnerr-guarded default assignment,
 * wyc-format.md §6.2) rely on: reading an unassigned variable must count
 * as "an error" for those to work. INSTANCE values whose class carries
 * WY_CLASS_ERROR are also errors, but that check is added in epic 3/4 once
 * instances exist.
 */
WY_INLINE bool wy_value_is_error(wy_value value)
{
    return value.type == WY_TYPE_TAG_ERROR;
}

/**
 * @brief Test whether a value is the Unset sentinel
 */
WY_INLINE bool wy_value_is_unset(wy_value value)
{
    return value.type == WY_TYPE_TAG_ERROR && value.data.gc_object == WY_NULL;
}

/**
 * @brief Test whether a value is truthy
 *
 * nil, false, 0, 0.0, the empty string, and Unset are false; everything
 * else - including every heap object this epic can produce - is true.
 * INSTANCE `__bool__` dispatch is added when instances exist (epic 4).
 *
 * Not WY_INLINE: the STR case needs wy_string's complete definition, which
 * value.h cannot see without creating a header cycle. Defined in string.c.
 */
bool wy_value_truthy(wy_value value);


WY_END_DECLS

#endif
