#ifndef WYRM_FUNCTION_H_
#define WYRM_FUNCTION_H_

#include <wyrm/module.h>
#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_function;
#ifndef __cplusplus
typedef struct wy_function wy_function;
#endif

extern const wy_object_type wy_function_type;

/**
 * A closure: a module-resident function prototype plus its captured values.
 *
 * `module` and `proto` are not owned/copied: `proto` points into `module`'s
 * already-loaded function-proto table, which lives as long as `module` does.
 */
struct wy_function
{
    wy_object object;
    wy_module* module;
    const wy_function_proto* proto;
    wy_uword ncaps;
    wy_value caps[];
};

/**
 * Allocate a closure over `proto`, copying `caps[0..ncaps)` as captures.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument (module/proto/out
 *   null, or ncaps > 0 with caps null), or WY_ERR_NOMEM
 */
wy_error wy_function_new(wy_context* context, wy_module* module, const wy_function_proto* proto,
    const wy_value* caps, wy_uword ncaps, wy_function** out);

WY_END_DECLS

#endif
