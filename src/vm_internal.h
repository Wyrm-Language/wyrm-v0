#ifndef WYRM_VM_INTERNAL_H_
#define WYRM_VM_INTERNAL_H_

#include <wyrm/frame.h>
#include <wyrm/fwd.h>
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

WY_END_DECLS

#endif
