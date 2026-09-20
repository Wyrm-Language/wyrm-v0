#ifndef WYRM_COROUTINE_H_
#define WYRM_COROUTINE_H_

#include <wyrm/fiber.h>
#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

/** A coroutine's lifecycle state (design_c_vm.md §3). */
typedef enum wy_co_state
{
    WY_CO_CREATED = 0,
    WY_CO_SUSPENDED,
    WY_CO_RUNNING,
    WY_CO_DONE,
} wy_co_state;

extern const wy_object_type wy_coroutine_type;

/**
 * A coroutine: its own fiber plus the bookkeeping `next`/`send`/`yield`/
 * `yield_from` need to drive it (design_c_vm.md §3).
 *
 * `resumer` is whichever fiber is currently waiting on this coroutine - the
 * caller of `next`/`send` for an undelegated coroutine, or (once delegated
 * via `yield_from`) the original top-level resumer, set directly on the
 * innermost coroutine so a `yield` there reaches it without walking the
 * delegate chain.
 *
 * `delegate`/`outer`/`delegate_dst` link a `yield_from`: `outer` is the
 * coroutine that ran `yield_from`, `outer->delegate` is the coroutine it
 * delegated to (this one, from the delegate's own perspective `outer` is
 * this field), and `delegate_dst` is where the delegate's return value
 * lands in the outer's own frame once it finishes.
 */
struct wy_coroutine
{
    wy_object object;
    wy_fiber* fiber;
    wy_fiber* resumer;
    wy_coroutine* delegate;
    wy_coroutine* outer;
    wy_value* delegate_dst;
    wy_u16 yield_base;
    wy_u8 state;   /**< wy_co_state */
    wy_value result;
};

/**
 * Allocate a coroutine wrapping `fiber` (already created via wy_fiber_create,
 * with its body frame pushed and `aux` set to the coroutine value - see
 * src/vm.c's WY_OP_CALL coroutine-construction branch).
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_coroutine_new_f(wy_context* context, wy_fiber* fiber, wy_coroutine** out);

/**
 * The coroutine `next`/`send` actually resume: `co` itself, or the innermost
 * link of its `delegate` chain (design_c_vm.md §3's "In next/send follow
 * while (co->delegate) co = co->delegate").
 */
WY_INLINE wy_coroutine* wy_coroutine_innermost_f(wy_coroutine* co)
{
    while (co != WY_NULL && co->delegate != WY_NULL) { co = co->delegate; }
    return co;
}

WY_END_DECLS

#endif
