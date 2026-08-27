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

extern const wyrm_object_type wyrm_box_type;

/**
 * @brief Box type - for lifting a name onto the heap.
 */
struct wyrm_box
{
    wyrm_object object;
    wyrm_value value;
};

wyrm_error wyrm_box_new_f(wyrm_context* self, wyrm_box** out);

/**
 * @brief Retrieve value stored in a box
 */
WYRM_INLINE wyrm_value wyrm_box_value_f(wyrm_box* box) { return box->value; }

/**
 * @brief Set value stored in a box
 */
WYRM_INLINE void wyrm_box_set_value_f(wyrm_box* box, wyrm_value value) { box->value = value; }

#ifdef __cplusplus
}
#endif

#endif
