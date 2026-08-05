#include <wyrm.h>

const wyrm_object_type wyrm_type_type = {
    .object = WYRM_OBJECT_STATIC_INITIALIZER(&wyrm_type_type),
    .gc_type = WYRM_TYPE_TAG_DTYPE,
};
