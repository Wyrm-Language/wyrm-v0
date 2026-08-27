#ifndef WYRM_WOPCODE_H
#define WYRM_WOPCODE_H

#include <wyrm/core.h>

WYRM_BEGIN_DECLS

typedef enum wy_opcode
{                                   // Arguments
    WYRM_OP_NOOP,                   // noop
    WYRM_OP_PASS,                   // pass

    WYRM_OP_LBYTE,                  // lbyte a0: target, f: value
    WYRM_OP_LSYM,                   // lsym a0: target a1: symbol table entry


    WYRM_OP_NEW_PRIMITIVE,          // primitive a0: target f: data type
    WYRM_OP_NEW_INSTANCE,           // instance a0: target a1: local class instance
    WYRM_OP_SET,                    // set a0: target a1: source


    WYRM_OP_LONG_START = 127,

} wy_opcode;

WYRM_INLINE wy_u32 wy_opcode_p0(wy_opcode op)
{
    return (wy_u32) op;
}

WYRM_INLINE wy_u32 wy_opcode_p1(wy_opcode op, wy_u16 arg)
{
    return (wy_u32) op | ((wy_u32) arg << 16);
}

WYRM_INLINE wy_u32 wy_opcode_p1f(wy_opcode op, wy_u16 arg, wy_u8 flag)
{
    return (wy_u32) op | ((wy_u32) arg << 16) | ((wy_u32) flag << 24);
}

WYRM_INLINE void wy_opcode_pack(wy_u32 dest[2], wy_opcode op, wy_u8 flag, wy_u16 arg0, wy_u16 arg1, wy_u16 arg2)
{
    dest[0] = wy_opcode_p1f(op, arg0, flag);
    dest[1] = (wy_u32) arg1 << 16 | arg2;
}

WYRM_INLINE void wy_opcode_p2(wy_u32 dest[2], wy_opcode op, wy_u16 a0, wy_u16 a1)
{
    wy_opcode_pack(dest, op, 0, a0, a1, 0);
}

WYRM_INLINE void wy_opcode_p3(wy_u32 dest[2], wy_opcode op, wy_u16 a0, wy_u16 a1, wy_u16 a3)
{
    wy_opcode_pack(dest, op, 0, a0, a1, a3);
}

WYRM_INLINE wy_opcode wy_opcode_get(const wy_u32 code[])
{
    return (wy_opcode) (code[0] & 0x000000ffu);
}

WYRM_INLINE wy_u8 wy_opcode_get_flag(const wy_u32 code[])
{
    return (wy_u8) ((code[0] >> 24) & 0x000000ffu);
}

WYRM_INLINE wy_u16 wy_opcode_get_a0(const wy_u32 code[])
{
    return (wy_u16) ((code[0] >> 16) & 0x0000ffffu);
}

WYRM_INLINE wy_u16 wy_opcode_get_a1(const wy_u32 code[])
{
    return (wy_u16) ((code[1] >> 16) & 0x0000ffffu);
}

WYRM_INLINE wy_u16 wy_opcode_get_a2(const wy_u32 code[])
{
    return (wy_u16) (code[1] & 0x0000ffffu);
}

WYRM_INLINE bool wy_opcode_is_long(const wy_u32 code[])
{
    return wy_opcode_get(code) >= WYRM_OP_LONG_START;
}

WYRM_END_DECLS

#endif
