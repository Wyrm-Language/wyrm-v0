#ifndef WYRM_WOPCODE_H
#define WYRM_WOPCODE_H

#include <wyrm/wcore.h>

WYRM_BEGIN_DECLS

typedef enum wy_opcode
{
    WYRM_OP_NOOP,
    WYRM_OP_PASS,

} wy_opcode;


WYRM_INLINE wy_u32 wy_opcode_p0(wy_opcode op) { return (wy_u32) op; }
WYRM_INLINE wy_opcode wy_opcode_get(wy_u32 code) { return (wy_opcode) code; }

WYRM_END_DECLS


#endif
