#ifndef WYRM_VM_H_
#define WYRM_VM_H_

#include <wyrm/context.h>
#include <wyrm/native.h>

WY_BEGIN_DECLS

wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len);

wy_exec_state wy_vm_exec_b_code(wy_context* context, wy_primitive c_data);

/**
 * Continuation of every bytecode frame; entry point for both a fresh call
 * and a resumption after a native call/GC safepoint. Declared here; the
 * dispatch loop itself lands in epic 2/M4 (src/vm.c).
 */
wy_exec_state wy_vm_run(wy_context* context, wy_primitive unused);

/**
 * Call `callee` synchronously from host/loader code (never from a native -
 * natives use wy_vm_call_continue instead, since C recursion into the VM
 * is not allowed there). Pushes a WY_RET_RESERVED frame and drives
 * wy_fiber_exec_f to completion.
 */
wy_error wy_vm_call_sync(wy_context* context, wy_value callee, const wy_value* args, wy_uword argc,
    wy_value* out, wy_uword nres);

/**
 * Call `callee`, resuming at `continuation` once it returns. For use from
 * native exec callables (design_c_vm.md §1.4).
 */
wy_error wy_vm_call_continue(wy_context* context, wy_exec_fn continuation, wy_value callee,
    const wy_value* args, wy_uword argc, wy_uword nres);

/* -- Native bridge (design_c_vm.md §1.3), src/vm_call.c -- */

/** Call a leaf native inline; see src/vm_call.c for the full contract. */
wy_error wy_vm_call_leaf_f(wy_context* ctx, wy_native* native, wy_value* args, wy_uword argc, wy_value* out, wy_uword nres);

/** Push an exec native's call via the reservation trampoline; see src/vm_call.c. */
wy_error wy_vm_call_exec_push_f(wy_context* ctx, wy_exec_fn continuation, wy_native* native,
    const wy_value* args, wy_uword argc, wy_uword nres);

/** Complete an exec native call once its continuation runs; see src/vm_call.c. */
wy_error wy_vm_native_await_complete_f(wy_context* ctx, wy_uword base_count, wy_value* dst, wy_uword nres);

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
