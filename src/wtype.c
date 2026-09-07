#include <wyrm.h>

const wy_object_type wy_type_type = {
    .object = WY_OBJECT_STATIC_INITIALIZER(&wy_type_type),
    .gc_type = WY_TYPE_TAG_DTYPE,
};
