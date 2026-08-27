#include <wyrm/wvm.h>
#include <wyrm/wopcode.h>

#include "wyrm/wcontext.h"


wyrm_error wy_vm_exec_bytecode(wyrm_context* ctx, size_t pos, const wy_u32 buffer[], size_t len)
{
    // No selected fiber is error
    if (ctx->current_fiber == WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_value local_accumulator = ctx->current_fiber->accumulator;
    if (pos >= len) { return WYRM_ERR_RANGE; }

    while (pos < len) {
        wy_u32 cur = buffer[pos];
        pos++;

        switch (wy_opcode_get(cur)) {
        case WYRM_OP_NOOP:
            break;

        case WYRM_OP_PASS:
            local_accumulator = wyrm_value_nil();
            break;

        default:
            return WYRM_ERR_INVAL;
        }

    }

    ctx->current_fiber->accumulator = local_accumulator;
    return WYRM_ERR_NONE;
}
