#include <wyrm/vm.h>
#include <wyrm/opcode.h>

#include "wyrm/context.h"


wyrm_error wy_vm_exec_bytecode(wyrm_context* ctx, size_t pos, const wy_u32 buffer[], size_t len)
{
    // No selected fiber is error
    if (ctx->current_fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (pos >= len) { return WYRM_ERR_RANGE; }

    while (pos < len) {
        const wy_u32* cur = &buffer[pos];
        if (wy_opcode_is_long(cur)) { pos++; }
        pos++;

        switch (wy_opcode_get(cur)) {
        case WYRM_OP_NOOP:
        case WYRM_OP_PASS:
            break;


        default:
            return WYRM_ERR_INVAL;
        }

    }

    return WYRM_ERR_NONE;
}
