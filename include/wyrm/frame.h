#ifndef WYRM_FRAME_H_
#define WYRM_FRAME_H_

#include <wyrm/exec_fn.h>
#include <wyrm/fwd.h>
#include <wyrm/module.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

/** Which trampoline convention a frame's return uses (design_c_vm.md §1.1). */
typedef enum wy_frame_kind
{
    WY_FRAME_NATIVE = 0,
    WY_FRAME_BYTECODE,
} wy_frame_kind;

/** How a frame's results reach its caller. */
typedef enum wy_ret_kind
{
    WY_RET_WINDOW = 0,     /**< copy results to ret_dst[0..nres) with nil backfill */
    WY_RET_RESERVED,       /**< results into reserved slots below caller base (C caller / native) */
    WY_RET_DISCARD,        /**< defers */
    WY_RET_CONSTRUCT,      /**< result[0] error -> that error, else the instance in aux */
    WY_RET_IMPORT,         /**< dependency init finished: fill free slots, ret_dst[0] = module */
    WY_RET_IMPORT_STAR,    /**< register wildcard, layer-2 fill */
    WY_RET_COROUTINE,      /**< body returned: co->result = result[0], DONE, switch back */
} wy_ret_kind;

/** Where a bytecode frame is in the dispatch loop across trampoline re-entries. */
typedef enum wy_frame_phase
{
    WY_PHASE_RUN = 0,
    WY_PHASE_AWAIT_NATIVE,
    WY_PHASE_RETURNING,
    WY_PHASE_FAILING,
} wy_frame_phase;

enum
{
    WY_FRAME_FLAG_METHOD = 1,
    WY_FRAME_FLAG_INIT   = 2,
};

struct wy_frame;
#ifndef __cplusplus
typedef struct wy_frame wy_frame;
#endif

/**
 * One call frame on a fiber's stack (design_c_vm.md §1.1).
 *
 * A native frame (`kind == WY_FRAME_NATIVE`) uses only `ret_kind` (always
 * WY_RET_RESERVED), `ret_nres`, `native` (the continuation to run once the
 * call returns) and `restore_base` - exactly the fields the old
 * `wy_fiber_frame` had, so the reservation trampoline
 * (`wy_fiber_push_frame_f`/`wy_fiber_pop_continuation_f`/
 * `wy_fiber_tail_call_f`) behaves identically to before this type existed.
 *
 * A bytecode frame (`kind == WY_FRAME_BYTECODE`) uses the rest; nothing
 * constructs one until epic 2/M4's dispatch loop.
 */
struct wy_frame
{
    wy_u8 kind;       /**< wy_frame_kind */
    wy_u8 ret_kind;   /**< wy_ret_kind */
    wy_u8 phase;      /**< wy_frame_phase */
    wy_u8 flags;      /**< WY_FRAME_FLAG_* */

    wy_u16 ret_nres;  /**< NATIVE: reserved result count. BYTECODE/WINDOW: caller's nres */
    wy_u16 p_count;

    wy_value* ret_dst;      /**< WINDOW: &caller->l[base]; RESERVED: first reserved slot */
    wy_value* p;             /**< P frame = stack base of this frame (BYTECODE only) */
    wy_value* l;             /**< L frame = p + p_count (BYTECODE only) */
    wy_value* restore_base;  /**< caller's wy_stack.base, restored on pop */

    const wy_u32* ip;                 /**< saved on every exit from the loop (BYTECODE only) */
    wy_module* module;                /**< BYTECODE only */
    const wy_function_proto* proto;   /**< BYTECODE only; NULL for module init */

    wy_exec_fn native;   /**< NATIVE: the continuation to run once the call returns */
    wy_pair* defers;     /**< (closure . mode) chain, most recent first (BYTECODE only) */

    wy_u16 ret_base;   /**< RETURNING: own L return window base */
    wy_u16 ret_count;  /**< RETURNING: own L return window count */

    wy_message* dispatch_msg;  /**< for `super` (BYTECODE method frames only) */
    wy_value dispatch_body;

    wy_value aux;  /**< per ret_kind: instance / import static / coroutine */
};

WY_END_DECLS

#endif
