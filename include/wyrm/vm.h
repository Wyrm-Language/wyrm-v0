#ifndef WYRM_VM_H_
#define WYRM_VM_H_

#include <wyrm/context.h>

WY_BEGIN_DECLS

wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len);

wy_exec_state wy_vm_exec_b_code(wy_context* context, wy_primitive c_data);

/**
 * Build a callable that enters bytecode at `address` in module `module_id`
 *
 * The pair is packed into the callable's payload, so a bytecode entry point
 * costs no more storage than a C one.
 */
WY_INLINE wy_exec_fn wy_exec_fn_create_b_code(wy_uword module_id, wy_uword address)
{
    return wy_exec_fn_create(wy_vm_exec_b_code, wy_exec_fn_b_code_pack(module_id, address));
}

WY_END_DECLS


#endif
