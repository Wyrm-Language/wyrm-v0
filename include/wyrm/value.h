#ifndef WYRM_VALUE_H_
#define WYRM_VALUE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/primitive.h>

WYRM_BEGIN_DECLS

/**
 * @brief A typed primitive
 */
struct wy_value
{
    wyrm_type_tag type;
    wy_primitive data;
};

enum {
    WYRM_PRIMITIVE_SIZE = sizeof(wy_primitive)
};

static_assert(WYRM_PRIMITIVE_SIZE >= sizeof(uintptr_t), "Primitive must allow storage of a pointer");

WYRM_END_DECLS

#endif
