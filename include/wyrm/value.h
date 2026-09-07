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

WY_END_DECLS

#endif
