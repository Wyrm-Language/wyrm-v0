#ifndef WYRM_VM_INTERNAL_H_
#define WYRM_VM_INTERNAL_H_

#include <wyrm/frame.h>
#include <wyrm/fwd.h>
#include <wyrm/instance.h>
#include <wyrm/message.h>
#include <wyrm/opcode.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

/** Register bit 15 (or bit 7 of an 8-bit ref) selects the P frame over L. */
WY_INLINE wy_value* wy_vm_reg_f(wy_frame* fr, wy_u16 r)
{
    return (r & WYRM_REG_P_BIT) ? &fr->p[r & 0x7fffu] : &fr->l[r];
}

WY_INLINE wy_value* wy_vm_reg8_f(wy_frame* fr, wy_u8 r)
{
    return (r & 0x80u) ? &fr->p[r & 0x7fu] : &fr->l[r];
}

/**
 * Copy up to `nres` of `src[0..count)` into `dst[0..nres)`, nil-filling any
 * of `dst` beyond `count` (design_c_vm.md §1.2's backfill rule).
 */
WY_INLINE void wy_vm_backfill_f(wy_value* dst, wy_uword nres, const wy_value* src, wy_uword count)
{
    wy_uword n = count < nres ? count : nres;
    for (wy_uword i = 0; i < n; i++) { dst[i] = src[i]; }
    for (wy_uword i = n; i < nres; i++) { dst[i] = wy_value_nil(); }
}

/* src/vm_ops.c - arithmetic/comparison/is/unary semantics (wyc-format.md §6.3) */
wy_error wy_vm_binop_f(wy_context* ctx, wy_u8 op, wy_value lhs, wy_value rhs, wy_value* out);
wy_error wy_vm_is_f(wy_context* ctx, wy_value value, wy_value type_operand, wy_value* out);
wy_error wy_vm_unary_f(wy_context* ctx, wy_u8 op, wy_value src, wy_value* out);
wy_error wy_vm_getidx_f(wy_context* ctx, wy_value obj, wy_value idx, wy_value* out);
wy_error wy_vm_setidx_f(wy_context* ctx, wy_value obj, wy_value idx, wy_value src);
wy_error wy_vm_in_f(wy_context* ctx, wy_value item, wy_value container, wy_value* out);

/* src/dispatch.c - message dispatch ranking (design_c_vm.md §7, epic 4/M3) */

enum { WY_DISPATCH_MAX_RECEIVERS = WY_OVERLOAD_MAX_ARITY };

/**
 * General ranking (port of `resolve_overload`,
 * wyrm_eval_parse_tree.py:2854-2891): the applicable overload of `msg` for
 * `receivers[0..n)`, lexicographically smallest distance vector wins.
 * `exclude`, if non-NULL, rules out any candidate whose distance vector is
 * lexicographically <= it (element-wise, `n` entries) - `super`'s "strictly
 * after `dispatch_body` in ranking order".
 *
 * @return WY_ERR_NONE with `*out` set, WY_ERR_UNBOUND (no candidate of
 *   arity `n` matches the receivers, or every match was excluded), or
 *   WY_ERR_AMBIGUOUS (`fault_msg` filled either way).
 */
wy_error wy_dispatch_resolve_f(wy_message* msg, const wy_value* receivers, wy_uword n,
    const wy_u16* exclude, const wy_overload** out, char* fault_msg, wy_uword fault_msg_size);

/**
 * The distance vector of the one overload in `msg` whose body is `body`
 * (pointer equality on the FUNCTION value), against `receivers[0..n)` -
 * `super`'s exclusion threshold: the currently-executing overload's own
 * ranking position, recomputed rather than stored anywhere.
 *
 * @return WY_ERR_NONE with `out_dist[0..n)` filled, or WY_ERR_UNBOUND if no
 *   overload of `msg` has that exact body (should not happen for a frame
 *   `super` is legal in).
 */
wy_error wy_dispatch_body_distance_f(wy_message* msg, wy_value body, const wy_value* receivers, wy_uword n, wy_u16* out_dist);

/**
 * The single-INSTANCE-receiver fast path (design §7): walk `inst->cls`,
 * `->super`, ... for the nearest ancestor whose `msg_map` names `msg`
 * directly, skipping the general ranking. Only correct when `msg` has no
 * arity-1 wildcard/PTYPE overload (`msg->has_wildcard_or_ptype_arity1`) -
 * checking that flag is the caller's job, same as checking the receiver
 * count and type; this function does not re-check it.
 *
 * @return WY_ERR_NONE with `*out_body` set, or WY_ERR_UNBOUND.
 */
wy_error wy_dispatch_single_instance_f(wy_message* msg, wy_instance* inst, wy_value* out_body);

WY_END_DECLS

#endif
