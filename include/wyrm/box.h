#ifndef WYRM_WBOX_H_
#define WYRM_WBOX_H_

#include <wyrm/core.h>
#include <wyrm/types.h>
#include <wyrm/object.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Wyrm Box
// ----------------------------------------------------------------------------

extern const wy_object_type wy_box_type;

/**
 * @brief Box type - for lifting a name onto the heap.
 */
struct wy_box
{
    wy_object object;
    wy_value value;
};

wy_error wy_box_new_f(wy_context* self, wy_box** out);

/**
 * @brief Retrieve value stored in a box
 */
WY_INLINE wy_value wy_box_value_f(wy_box* box) { return box->value; }

/**
 * @brief Set value stored in a box
 */
WY_INLINE void wy_box_set_value_f(wy_box* box, wy_value value) { box->value = value; }

#ifdef __cplusplus
}
#endif

#endif
