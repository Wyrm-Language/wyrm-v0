#ifndef WYRM_ERROR_H_
#define WYRM_ERROR_H_

#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_error_obj;
#ifndef __cplusplus
typedef struct wy_error_obj wy_error_obj;
#endif

extern const wy_object_type wy_error_obj_type;

/**
 * A realised error/exception value (design_c_vm.md §4). `cls` is the error's
 * class; it stays WY_NULL until epic 4 realises class objects. `payload` is
 * an arbitrary attached value.
 */
struct wy_error_obj
{
    wy_object object;
    wy_error code;             /**< optional VM fault code; NONE for user errors */
    wy_class* cls;
    wy_string* what;
    wy_value payload;
};

/**
 * Allocate an error object.
 *
 * `cls` and `what` are not copied; the error object shares ownership of them
 * with whoever else references them (both are GC-tracked, kept alive by the
 * error object's children_iter).
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL when `out` is null, or WY_ERR_NOMEM
 */
wy_error wy_error_obj_new(wy_context* context, wy_class* cls, wy_string* what, wy_value payload, wy_error_obj** out);

WY_END_DECLS

#endif
