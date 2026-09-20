#ifndef WYRM_DICT_H_
#define WYRM_DICT_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/value.h>
#include <wyrm/object.h>

WY_BEGIN_DECLS

/**
 * Key Hash Value
 */
typedef struct wy_key_hash_value
{
    wy_value key;
    wy_uword key_hash;
    wy_value value;
} wy_key_hash_value;

/**
 * Dictionary type
 */
struct wy_dict
{
    wy_object object;

    wy_allocator* allocator;

    wy_uword count;

    wy_key_hash_value* dense;
    wy_uword dense_capacity;

    wy_uword* sparse;
    wy_uword sparse_capacity;
};

extern const wy_object_type wy_type_table;

wy_error wy_dict_new(wy_context* self, wy_dict** out);
wy_value* wy_dict_get(wy_context* context, wy_dict* self, wy_type_tag tag, wy_primitive value);
wy_error wy_dict_set(wy_context* context, wy_dict* self, wy_type_tag key_type, wy_primitive key_value, wy_type_tag value_type, wy_primitive value);

/**
 * Remove `key` from the dict, writing the removed value to `*out` (when
 * non-null) before dropping the entry.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null dict, WY_ERR_BAD_TYPE for an
 *   unhashable key, or WY_ERR_KEY when the key is not present
 */
wy_error wy_dict_remove(wy_context* context, wy_dict* self, wy_type_tag key_type, wy_primitive key_value, wy_value* out);



WY_END_DECLS

#endif
