#ifndef WYRM_NATIVE_H_
#define WYRM_NATIVE_H_

#include <wyrm/exec_fn.h>
#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_native;
#ifndef __cplusplus
typedef struct wy_native wy_native;
#endif

enum wy_native_kind
{
    WY_NATIVE_LEAF = 0,
    WY_NATIVE_EXEC = 1,
};

/**
 * A signature for a "leaf" native: runs to completion synchronously, writing
 * up to `nres` results directly, with no access to the fiber machinery an
 * WY_NATIVE_EXEC callable would need.
 */
typedef wy_error (*wy_native_leaf_fn)(wy_context* context, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres);

extern const wy_object_type wy_native_type;

/**
 * A native (host-provided) callable. `name` is machine-lifetime (a symbol),
 * not GC-tracked.
 */
struct wy_native
{
    wy_object object;
    wy_symbol name;
    wy_u8 kind;      /**< WY_NATIVE_LEAF or WY_NATIVE_EXEC */
    wy_u8 min_argc;
    wy_u8 max_argc;
    union {
        wy_native_leaf_fn leaf;
        wy_exec_fn exec;
    } fn;
};

/**
 * Allocate a native wrapping a synchronous leaf function.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_native_leaf_new(wy_context* context, wy_symbol name, wy_u8 min_argc, wy_u8 max_argc,
    wy_native_leaf_fn leaf_fn, wy_native** out);

/**
 * Allocate a native wrapping an exec-fn callable (participates in the fiber's
 * pending/continuation machinery).
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_native_exec_new(wy_context* context, wy_symbol name, wy_u8 min_argc, wy_u8 max_argc,
    wy_exec_fn exec_fn, wy_native** out);

WY_END_DECLS

#endif
