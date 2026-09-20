#include <wyrm/vm.h>
#include <wyrm/opcode.h>

#include "wyrm/context.h"


wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len)
{
    // No selected fiber is error
    if (ctx->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (pos >= len) { return WY_ERR_RANGE; }

    while (pos < len) {
        const wy_u32* cur = &buffer[pos];
        if (wy_opcode_is_long(cur)) { pos++; }
        pos++;

        switch (wy_opcode_get(cur)) {
        case WY_OP_NOOP:
        case WY_OP_PASS:
            break;


        default:
            return WY_ERR_INVAL;
        }

    }

    return WY_ERR_NONE;
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
