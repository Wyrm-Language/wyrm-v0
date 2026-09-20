#include <wyrm/vm.h>

#include "wyrm/context.h"


/**
 * Stub pending the epic 2 interpreter loop rewrite against the adopted
 * opcode.h encoding (E1/M2): always fails rather than decoding with the
 * retired enum.
 */
wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len)
{
    WY_UNUSED(ctx);
    WY_UNUSED(pos);
    WY_UNUSED(buffer);
    WY_UNUSED(len);
    return WY_ERR_INVAL;
}


/**
 * Enter bytecode from a callable payload
 *
 * Unpacks the (module id, address) pair the callable carries and resolves
 * the module the address belongs to. The interpreter loop itself is not
 * wired up yet, so this returns immediately.
 *
 * @param context Context whose current fiber holds the call frame
 * @param c_data Packed module id and code address
 * @return Execution state for the fiber loop
 */
wy_exec_state wy_vm_exec_b_code(wy_context* context, wy_primitive c_data)
{
    WY_ASSERT(context != WY_NULL);

    wy_uword module_id = wy_exec_fn_b_code_module_id(c_data);
    wy_uword address = wy_exec_fn_b_code_address(c_data);

    wy_module* module = wy_context_get_module(context, module_id);
    WY_UNUSED(module);
    WY_UNUSED(address);

    return WY_EXEC_DONE;
}
