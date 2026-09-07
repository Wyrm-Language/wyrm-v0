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
