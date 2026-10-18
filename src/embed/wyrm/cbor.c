/* Generated from cbor by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

static const uint8_t cbor_header[] = {
    0xA3, 0x61, 0x6E, 0x64, 0x63, 0x62, 0x6F, 0x72, 0x61, 0x67, 0x0F, 0x61,
    0x6C, 0x03,
};

static const uint8_t cbor_statics[] = {
    0x89, 0x63, 0x6E, 0x69, 0x6C, 0x64, 0x62, 0x6F, 0x6F, 0x6C, 0x65, 0x66,
    0x6C, 0x6F, 0x61, 0x74, 0x64, 0x6C, 0x69, 0x73, 0x74, 0x64, 0x64, 0x69,
    0x63, 0x74, 0x78, 0x2E, 0x63, 0x62, 0x6F, 0x72, 0x3A, 0x20, 0x61, 0x20,
    0x6D, 0x61, 0x70, 0x20, 0x6B, 0x65, 0x79, 0x20, 0x6D, 0x75, 0x73, 0x74,
    0x20, 0x62, 0x65, 0x20, 0x61, 0x6E, 0x20, 0x69, 0x6E, 0x74, 0x2C, 0x20,
    0x61, 0x20, 0x73, 0x74, 0x72, 0x20, 0x6F, 0x72, 0x20, 0x62, 0x79, 0x74,
    0x65, 0x73, 0x63, 0x69, 0x6E, 0x74, 0x63, 0x73, 0x74, 0x72, 0x65, 0x65,
    0x72, 0x72, 0x6F, 0x72,
};

static const uint8_t cbor_symbols[] = {
    0x82, 0x66, 0x61, 0x70, 0x70, 0x65, 0x6E, 0x64, 0x68, 0x70, 0x61, 0x63,
    0x6B, 0x5F, 0x66, 0x36, 0x34,
};

static const uint8_t cbor_functions[] = {
    0x85, 0xA5, 0x61, 0x6E, 0x68, 0x68, 0x65, 0x61, 0x64, 0x5F, 0x6C, 0x65,
    0x6E, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x61, 0x6E, 0x61, 0x6C, 0x02,
    0x61, 0x63, 0x18, 0x21, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x64, 0x68,
    0x65, 0x61, 0x64, 0x61, 0x70, 0x82, 0xA1, 0x61, 0x6E, 0x65, 0x6D, 0x61,
    0x6A, 0x6F, 0x72, 0xA1, 0x61, 0x6E, 0x61, 0x6E, 0x61, 0x6C, 0x08, 0x61,
    0x63, 0x18, 0x3D, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x66, 0x65, 0x6E,
    0x63, 0x6F, 0x64, 0x65, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x65, 0x76,
    0x61, 0x6C, 0x75, 0x65, 0x61, 0x6C, 0x02, 0x61, 0x63, 0x18, 0xD0, 0x61,
    0x66, 0x00, 0xA5, 0x61, 0x6E, 0x64, 0x5F, 0x6B, 0x65, 0x79, 0x61, 0x70,
    0x81, 0xA1, 0x61, 0x6E, 0x63, 0x6B, 0x65, 0x79, 0x61, 0x6C, 0x02, 0x61,
    0x63, 0x18, 0xD5, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x66, 0x5F, 0x76,
    0x61, 0x6C, 0x75, 0x65, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x65, 0x76,
    0x61, 0x6C, 0x75, 0x65, 0x61, 0x6C, 0x14, 0x61, 0x63, 0x18, 0xF3, 0x61,
    0x66, 0x00,
};

static const uint8_t cbor_classes[] = {
    0x80,
};

static const uint8_t cbor_messages[] = {
    0x82, 0xA1, 0x61, 0x70, 0x81, 0x00, 0xA1, 0x61, 0x70, 0x81, 0x01,
};

static const uint8_t cbor_exports[] = {
    0xAC, 0x68, 0x68, 0x65, 0x61, 0x64, 0x5F, 0x6C, 0x65, 0x6E, 0x00, 0x64,
    0x68, 0x65, 0x61, 0x64, 0x01, 0x66, 0x65, 0x6E, 0x63, 0x6F, 0x64, 0x65,
    0x02, 0x64, 0x5F, 0x6B, 0x65, 0x79, 0x03, 0x66, 0x5F, 0x76, 0x61, 0x6C,
    0x75, 0x65, 0x04, 0x69, 0x55, 0x33, 0x32, 0x5F, 0x4C, 0x49, 0x4D, 0x49,
    0x54, 0x05, 0x6A, 0x4D, 0x41, 0x4A, 0x4F, 0x52, 0x5F, 0x55, 0x49, 0x4E,
    0x54, 0x06, 0x6C, 0x4D, 0x41, 0x4A, 0x4F, 0x52, 0x5F, 0x4E, 0x45, 0x47,
    0x49, 0x4E, 0x54, 0x07, 0x6B, 0x4D, 0x41, 0x4A, 0x4F, 0x52, 0x5F, 0x42,
    0x59, 0x54, 0x45, 0x53, 0x08, 0x6A, 0x4D, 0x41, 0x4A, 0x4F, 0x52, 0x5F,
    0x54, 0x45, 0x58, 0x54, 0x09, 0x6B, 0x4D, 0x41, 0x4A, 0x4F, 0x52, 0x5F,
    0x41, 0x52, 0x52, 0x41, 0x59, 0x0A, 0x69, 0x4D, 0x41, 0x4A, 0x4F, 0x52,
    0x5F, 0x4D, 0x41, 0x50, 0x0B,
};

static const uint8_t cbor_free[] = {
    0xA3, 0x65, 0x62, 0x79, 0x74, 0x65, 0x73, 0x0C, 0x65, 0x65, 0x72, 0x72,
    0x6F, 0x72, 0x0D, 0x63, 0x6C, 0x65, 0x6E, 0x0E,
};

static const uint32_t cbor_code[] = {
    0x00010240,                /* 0000  i8 L1 <- 2 */
    0x00022040,                /* 0001  i8 L2 <- 32 */
    0x0000008A, 0x00010002,    /* 0002  pow L0 <- L1 ** L2 */
    0x00050043,                /* 0004  gset g5 <- L0   (U32_LIMIT) */
    0x00000040,                /* 0005  i8 L0 <- 0 */
    0x00060043,                /* 0006  gset g6 <- L0   (MAJOR_UINT) */
    0x00000140,                /* 0007  i8 L0 <- 1 */
    0x00070043,                /* 0008  gset g7 <- L0   (MAJOR_NEGINT) */
    0x00000240,                /* 0009  i8 L0 <- 2 */
    0x00080043,                /* 000A  gset g8 <- L0   (MAJOR_BYTES) */
    0x00000340,                /* 000B  i8 L0 <- 3 */
    0x00090043,                /* 000C  gset g9 <- L0   (MAJOR_TEXT) */
    0x00000440,                /* 000D  i8 L0 <- 4 */
    0x000A0043,                /* 000E  gset g10 <- L0   (MAJOR_ARRAY) */
    0x00000540,                /* 000F  i8 L0 <- 5 */
    0x000B0043,                /* 0010  gset g11 <- L0   (MAJOR_MAP) */
    0x000000A8, 0x00000000,    /* 0011  closure L0 <- fn#0, 0 caps */
    0x00000043,                /* 0013  gset g0 <- L0   (head_len) */
    0x000000A8, 0x00010000,    /* 0014  closure L0 <- fn#1, 0 caps */
    0x00010043,                /* 0016  gset g1 <- L0   (head) */
    0x000000A8, 0x00020000,    /* 0017  closure L0 <- fn#2, 0 caps */
    0x00020043,                /* 0019  gset g2 <- L0   (encode) */
    0x000000A8, 0x00030000,    /* 001A  closure L0 <- fn#3, 0 caps */
    0x00030043,                /* 001C  gset g3 <- L0   (_key) */
    0x000000A8, 0x00040000,    /* 001D  closure L0 <- fn#4, 0 caps */
    0x00040043,                /* 001F  gset g4 <- L0   (_value) */
    0x00000002,                /* 0020  return count=0 */
    0x00001840,                /* 0021  i8 L0 <- 24 */
    0x00010091, 0x80000000,    /* 0022  lt L1 <- P0 < L0 */
    0x0002014B,                /* 0024  jf L1, +2 */
    0x00000140,                /* 0025  i8 L0 <- 1 */
    0x00000102,                /* 0026  return L0 count=1 */
    0x000000C0, 0x00000100,    /* 0027  i32 L0 <- 256 */
    0x00010091, 0x80000000,    /* 0029  lt L1 <- P0 < L0 */
    0x0002014B,                /* 002B  jf L1, +2 */
    0x00000240,                /* 002C  i8 L0 <- 2 */
    0x00000102,                /* 002D  return L0 count=1 */
    0x000000C0, 0x00010000,    /* 002E  i32 L0 <- 65536 */
    0x00010091, 0x80000000,    /* 0030  lt L1 <- P0 < L0 */
    0x0002014B,                /* 0032  jf L1, +2 */
    0x00000340,                /* 0033  i8 L0 <- 3 */
    0x00000102,                /* 0034  return L0 count=1 */
    0x00050042,                /* 0035  gget L0 <- g5   (U32_LIMIT) */
    0x00010091, 0x80000000,    /* 0036  lt L1 <- P0 < L0 */
    0x0002014B,                /* 0038  jf L1, +2 */
    0x00000540,                /* 0039  i8 L0 <- 5 */
    0x00000102,                /* 003A  return L0 count=1 */
    0x00000940,                /* 003B  i8 L0 <- 9 */
    0x00000102,                /* 003C  return L0 count=1 */
    0x000C0342,                /* 003D  gget L3 <- g12   (bytes) */
    0x00040040,                /* 003E  i8 L4 <- 0 */
    0x000301A0, 0x00010000,    /* 003F  call base=L3 argc=1 nres=1 */
    0x00000341,                /* 0041  move L0 <- L3 */
    0x00032040,                /* 0042  i8 L3 <- 32 */
    0x00010087, 0x80000003,    /* 0043  mul L1 <- P0 * L3 */
    0x00031840,                /* 0045  i8 L3 <- 24 */
    0x00040091, 0x80010003,    /* 0046  lt L4 <- P1 < L3 */
    0x0006044B,                /* 0048  jf L4, +6 */
    0x00030041,                /* 0049  move L3 <- L0 */
    0x00040085, 0x00018001,    /* 004A  add L4 <- L1 + P1 */
    0x000301A2, 0x00000001,    /* 004C  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x0080004F,                /* 004E  jmp +128 */
    0x000300C0, 0x00000100,    /* 004F  i32 L3 <- 256 */
    0x00040091, 0x80010003,    /* 0051  lt L4 <- P1 < L3 */
    0x000C044B,                /* 0053  jf L4, +12 */
    0x00030041,                /* 0054  move L3 <- L0 */
    0x00041840,                /* 0055  i8 L4 <- 24 */
    0x00050085, 0x00010004,    /* 0056  add L5 <- L1 + L4 */
    0x00040541,                /* 0058  move L4 <- L5 */
    0x000301A2, 0x00000001,    /* 0059  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 005B  move L3 <- L0 */
    0x00048141,                /* 005C  move L4 <- P1 */
    0x000301A2, 0x00000001,    /* 005D  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x006F004F,                /* 005F  jmp +111 */
    0x000300C0, 0x00010000,    /* 0060  i32 L3 <- 65536 */
    0x00040091, 0x80010003,    /* 0062  lt L4 <- P1 < L3 */
    0x001B044B,                /* 0064  jf L4, +27 */
    0x00030041,                /* 0065  move L3 <- L0 */
    0x00041940,                /* 0066  i8 L4 <- 25 */
    0x00050085, 0x00010004,    /* 0067  add L5 <- L1 + L4 */
    0x00040541,                /* 0069  move L4 <- L5 */
    0x000301A2, 0x00000001,    /* 006A  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 006C  move L3 <- L0 */
    0x00040840,                /* 006D  i8 L4 <- 8 */
    0x0005008E, 0x80010004,    /* 006E  shr L5 <- P1 >> L4 */
    0x000600C0, 0x000000FF,    /* 0070  i32 L6 <- 255 */
    0x0007008B, 0x00050006,    /* 0072  band L7 <- L5 & L6 */
    0x00040741,                /* 0074  move L4 <- L7 */
    0x000301A2, 0x00000001,    /* 0075  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 0077  move L3 <- L0 */
    0x000400C0, 0x000000FF,    /* 0078  i32 L4 <- 255 */
    0x0005008B, 0x80010004,    /* 007A  band L5 <- P1 & L4 */
    0x00040541,                /* 007C  move L4 <- L5 */
    0x000301A2, 0x00000001,    /* 007D  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x004F004F,                /* 007F  jmp +79 */
    0x00050342,                /* 0080  gget L3 <- g5   (U32_LIMIT) */
    0x00040091, 0x80010003,    /* 0081  lt L4 <- P1 < L3 */
    0x0031044B,                /* 0083  jf L4, +49 */
    0x00030041,                /* 0084  move L3 <- L0 */
    0x00041A40,                /* 0085  i8 L4 <- 26 */
    0x00050085, 0x00010004,    /* 0086  add L5 <- L1 + L4 */
    0x00040541,                /* 0088  move L4 <- L5 */
    0x000301A2, 0x00000001,    /* 0089  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 008B  move L3 <- L0 */
    0x00041840,                /* 008C  i8 L4 <- 24 */
    0x0005008E, 0x80010004,    /* 008D  shr L5 <- P1 >> L4 */
    0x000600C0, 0x000000FF,    /* 008F  i32 L6 <- 255 */
    0x0007008B, 0x00050006,    /* 0091  band L7 <- L5 & L6 */
    0x00040741,                /* 0093  move L4 <- L7 */
    0x000301A2, 0x00000001,    /* 0094  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 0096  move L3 <- L0 */
    0x00041040,                /* 0097  i8 L4 <- 16 */
    0x0005008E, 0x80010004,    /* 0098  shr L5 <- P1 >> L4 */
    0x000600C0, 0x000000FF,    /* 009A  i32 L6 <- 255 */
    0x0007008B, 0x00050006,    /* 009C  band L7 <- L5 & L6 */
    0x00040741,                /* 009E  move L4 <- L7 */
    0x000301A2, 0x00000001,    /* 009F  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 00A1  move L3 <- L0 */
    0x00040840,                /* 00A2  i8 L4 <- 8 */
    0x0005008E, 0x80010004,    /* 00A3  shr L5 <- P1 >> L4 */
    0x000600C0, 0x000000FF,    /* 00A5  i32 L6 <- 255 */
    0x0007008B, 0x00050006,    /* 00A7  band L7 <- L5 & L6 */
    0x00040741,                /* 00A9  move L4 <- L7 */
    0x000301A2, 0x00000001,    /* 00AA  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030041,                /* 00AC  move L3 <- L0 */
    0x000400C0, 0x000000FF,    /* 00AD  i32 L4 <- 255 */
    0x0005008B, 0x80010004,    /* 00AF  band L5 <- P1 & L4 */
    0x00040541,                /* 00B1  move L4 <- L5 */
    0x000301A2, 0x00000001,    /* 00B2  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x001A004F,                /* 00B4  jmp +26 */
    0x00030041,                /* 00B5  move L3 <- L0 */
    0x00041B40,                /* 00B6  i8 L4 <- 27 */
    0x00050085, 0x00010004,    /* 00B7  add L5 <- L1 + L4 */
    0x00040541,                /* 00B9  move L4 <- L5 */
    0x000301A2, 0x00000001,    /* 00BA  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00023840,                /* 00BC  i8 L2 <- 56 */
    0x00030040,                /* 00BD  i8 L3 <- 0 */
    0x00040094, 0x00020003,    /* 00BE  ge L4 <- L2 >= L3 */
    0x000E044B,                /* 00C0  jf L4, +14 */
    0x00030041,                /* 00C1  move L3 <- L0 */
    0x0004008E, 0x80010002,    /* 00C2  shr L4 <- P1 >> L2 */
    0x000500C0, 0x000000FF,    /* 00C4  i32 L5 <- 255 */
    0x0006008B, 0x00040005,    /* 00C6  band L6 <- L4 & L5 */
    0x00040641,                /* 00C8  move L4 <- L6 */
    0x000301A2, 0x00000001,    /* 00C9  msg base=L3 argc=1 msg#0 nres=1   (append) */
    0x00030840,                /* 00CB  i8 L3 <- 8 */
    0x00020086, 0x00020003,    /* 00CC  sub L2 <- L2 - L3 */
    0xFFEE004F,                /* 00CE  jmp -18 */
    0x00000102,                /* 00CF  return L0 count=1 */
    0x00040042,                /* 00D0  gget L0 <- g4   (_value) */
    0x00018041,                /* 00D1  move L1 <- P0 */
    0x000001A0, 0x00010000,    /* 00D2  call base=L0 argc=1 nres=1 */
    0x00000102,                /* 00D4  return L0 count=1 */
    0x00000146,                /* 00D5  lconst L1 <- static#0   ("nil") */
    0x00000097, 0x80000001,    /* 00D6  is L0 <- P0 is L1 */
    0x000F004C,                /* 00D8  jt L0, +15 */
    0x00010146,                /* 00D9  lconst L1 <- static#1   ("bool") */
    0x00000097, 0x80000001,    /* 00DA  is L0 <- P0 is L1 */
    0x000B004C,                /* 00DC  jt L0, +11 */
    0x00020146,                /* 00DD  lconst L1 <- static#2   ("float") */
    0x00000097, 0x80000001,    /* 00DE  is L0 <- P0 is L1 */
    0x0007004C,                /* 00E0  jt L0, +7 */
    0x00030146,                /* 00E1  lconst L1 <- static#3   ("list") */
    0x00000097, 0x80000001,    /* 00E2  is L0 <- P0 is L1 */
    0x0003004C,                /* 00E4  jt L0, +3 */
    0x00040146,                /* 00E5  lconst L1 <- static#4   ("dict") */
    0x00000097, 0x80000001,    /* 00E6  is L0 <- P0 is L1 */
    0x0005004B,                /* 00E8  jf L0, +5 */
    0x000D0042,                /* 00E9  gget L0 <- g13   (error) */
    0x00050146,                /* 00EA  lconst L1 <- static#5   ("cbor: a map key must be an int, a str or bytes") */
    0x000001A0, 0x00010000,    /* 00EB  call base=L0 argc=1 nres=1 */
    0x00000102,                /* 00ED  return L0 count=1 */
    0x00040042,                /* 00EE  gget L0 <- g4   (_value) */
    0x00018041,                /* 00EF  move L1 <- P0 */
    0x000001A0, 0x00010000,    /* 00F0  call base=L0 argc=1 nres=1 */
    0x00000102,                /* 00F2  return L0 count=1 */
    0x00000F46,                /* 00F3  lconst L15 <- static#0   ("nil") */
    0x00100097, 0x8000000F,    /* 00F4  is L16 <- P0 is L15 */
    0x000B104B,                /* 00F6  jf L16, +11 */
    0x000C0F42,                /* 00F7  gget L15 <- g12   (bytes) */
    0x00100040,                /* 00F8  i8 L16 <- 0 */
    0x000F01A0, 0x00010000,    /* 00F9  call base=L15 argc=1 nres=1 */
    0x00010F41,                /* 00FB  move L1 <- L15 */
    0x000F0141,                /* 00FC  move L15 <- L1 */
    0x001000C0, 0x000000F6,    /* 00FD  i32 L16 <- 246 */
    0x000F01A2, 0x00000001,    /* 00FF  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x00010102,                /* 0101  return L1 count=1 */
    0x00010F46,                /* 0102  lconst L15 <- static#1   ("bool") */
    0x00100097, 0x8000000F,    /* 0103  is L16 <- P0 is L15 */
    0x0012104B,                /* 0105  jf L16, +18 */
    0x000C0F42,                /* 0106  gget L15 <- g12   (bytes) */
    0x00100040,                /* 0107  i8 L16 <- 0 */
    0x000F01A0, 0x00010000,    /* 0108  call base=L15 argc=1 nres=1 */
    0x00020F41,                /* 010A  move L2 <- L15 */
    0x0006804B,                /* 010B  jf P0, +6 */
    0x000F0241,                /* 010C  move L15 <- L2 */
    0x001000C0, 0x000000F5,    /* 010D  i32 L16 <- 245 */
    0x000F01A2, 0x00000001,    /* 010F  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x0005004F,                /* 0111  jmp +5 */
    0x000F0241,                /* 0112  move L15 <- L2 */
    0x001000C0, 0x000000F4,    /* 0113  i32 L16 <- 244 */
    0x000F01A2, 0x00000001,    /* 0115  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x00020102,                /* 0117  return L2 count=1 */
    0x00060F46,                /* 0118  lconst L15 <- static#6   ("int") */
    0x00100097, 0x8000000F,    /* 0119  is L16 <- P0 is L15 */
    0x0014104B,                /* 011B  jf L16, +20 */
    0x000F0040,                /* 011C  i8 L15 <- 0 */
    0x00100094, 0x8000000F,    /* 011D  ge L16 <- P0 >= L15 */
    0x0006104B,                /* 011F  jf L16, +6 */
    0x00010F42,                /* 0120  gget L15 <- g1   (head) */
    0x00061042,                /* 0121  gget L16 <- g6   (MAJOR_UINT) */
    0x00118041,                /* 0122  move L17 <- P0 */
    0x000F02A0, 0x00010000,    /* 0123  call base=L15 argc=2 nres=1 */
    0x000F0102,                /* 0125  return L15 count=1 */
    0x00010F42,                /* 0126  gget L15 <- g1   (head) */
    0x00071042,                /* 0127  gget L16 <- g7   (MAJOR_NEGINT) */
    0x00110140,                /* 0128  i8 L17 <- 1 */
    0x00121148,                /* 0129  neg L18 <- L17 */
    0x00130086, 0x00128000,    /* 012A  sub L19 <- L18 - P0 */
    0x00111341,                /* 012C  move L17 <- L19 */
    0x000F02A0, 0x00010000,    /* 012D  call base=L15 argc=2 nres=1 */
    0x000F0102,                /* 012F  return L15 count=1 */
    0x00020F46,                /* 0130  lconst L15 <- static#2   ("float") */
    0x00100097, 0x8000000F,    /* 0131  is L16 <- P0 is L15 */
    0x0023104B,                /* 0133  jf L16, +35 */
    0x000C0F42,                /* 0134  gget L15 <- g12   (bytes) */
    0x00100840,                /* 0135  i8 L16 <- 8 */
    0x000F01A0, 0x00010000,    /* 0136  call base=L15 argc=1 nres=1 */
    0x00030F41,                /* 0138  move L3 <- L15 */
    0x000F0341,                /* 0139  move L15 <- L3 */
    0x00100040,                /* 013A  i8 L16 <- 0 */
    0x00118041,                /* 013B  move L17 <- P0 */
    0x000F02A2, 0x00010001,    /* 013C  msg base=L15 argc=2 msg#1 nres=1   (pack_f64) */
    0x000C0F42,                /* 013E  gget L15 <- g12   (bytes) */
    0x00100040,                /* 013F  i8 L16 <- 0 */
    0x000F01A0, 0x00010000,    /* 0140  call base=L15 argc=1 nres=1 */
    0x00040F41,                /* 0142  move L4 <- L15 */
    0x000F0441,                /* 0143  move L15 <- L4 */
    0x001000C0, 0x000000FB,    /* 0144  i32 L16 <- 251 */
    0x000F01A2, 0x00000001,    /* 0146  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x00050740,                /* 0148  i8 L5 <- 7 */
    0x000F0040,                /* 0149  i8 L15 <- 0 */
    0x00100094, 0x0005000F,    /* 014A  ge L16 <- L5 >= L15 */
    0x0009104B,                /* 014C  jf L16, +9 */
    0x000F0441,                /* 014D  move L15 <- L4 */
    0x00100098, 0x00030005,    /* 014E  getidx L16 <- L3[L5] */
    0x000F01A2, 0x00000001,    /* 0150  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x000F0140,                /* 0152  i8 L15 <- 1 */
    0x00050086, 0x0005000F,    /* 0153  sub L5 <- L5 - L15 */
    0xFFF3004F,                /* 0155  jmp -13 */
    0x00040102,                /* 0156  return L4 count=1 */
    0x00070F46,                /* 0157  lconst L15 <- static#7   ("str") */
    0x00100097, 0x8000000F,    /* 0158  is L16 <- P0 is L15 */
    0x0013104B,                /* 015A  jf L16, +19 */
    0x000C0F42,                /* 015B  gget L15 <- g12   (bytes) */
    0x00108041,                /* 015C  move L16 <- P0 */
    0x000F01A0, 0x00010000,    /* 015D  call base=L15 argc=1 nres=1 */
    0x00060F41,                /* 015F  move L6 <- L15 */
    0x00010F42,                /* 0160  gget L15 <- g1   (head) */
    0x00091042,                /* 0161  gget L16 <- g9   (MAJOR_TEXT) */
    0x000E1142,                /* 0162  gget L17 <- g14   (len) */
    0x00120641,                /* 0163  move L18 <- L6 */
    0x001101A0, 0x00010000,    /* 0164  call base=L17 argc=1 nres=1 */
    0x000F02A0, 0x00010000,    /* 0166  call base=L15 argc=2 nres=1 */
    0x00070F41,                /* 0168  move L7 <- L15 */
    0x000F0741,                /* 0169  move L15 <- L7 */
    0x00100641,                /* 016A  move L16 <- L6 */
    0x000F01A2, 0x00000001,    /* 016B  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x00070102,                /* 016D  return L7 count=1 */
    0x00040F46,                /* 016E  lconst L15 <- static#4   ("dict") */
    0x00100097, 0x8000000F,    /* 016F  is L16 <- P0 is L15 */
    0x002C104B,                /* 0171  jf L16, +44 */
    0x00010F42,                /* 0172  gget L15 <- g1   (head) */
    0x000B1042,                /* 0173  gget L16 <- g11   (MAJOR_MAP) */
    0x000E1142,                /* 0174  gget L17 <- g14   (len) */
    0x00128041,                /* 0175  move L18 <- P0 */
    0x001101A0, 0x00010000,    /* 0176  call base=L17 argc=1 nres=1 */
    0x000F02A0, 0x00010000,    /* 0178  call base=L15 argc=2 nres=1 */
    0x00080F41,                /* 017A  move L8 <- L15 */
    0x000F00AE, 0x80000000,    /* 017B  iter L15 <- P0 */
    0x000900AF, 0x000F001E,    /* 017D  itnext L9 <- L15, done +30 */
    0x00031042,                /* 017F  gget L16 <- g3   (_key) */
    0x00110941,                /* 0180  move L17 <- L9 */
    0x001001A0, 0x00010000,    /* 0181  call base=L16 argc=1 nres=1 */
    0x000A1041,                /* 0183  move L10 <- L16 */
    0x00081046,                /* 0184  lconst L16 <- static#8   ("error") */
    0x00110097, 0x000A0010,    /* 0185  is L17 <- L10 is L16 */
    0x0001114B,                /* 0187  jf L17, +1 */
    0x000A0102,                /* 0188  return L10 count=1 */
    0x00041042,                /* 0189  gget L16 <- g4   (_value) */
    0x00110098, 0x80000009,    /* 018A  getidx L17 <- P0[L9] */
    0x001001A0, 0x00010000,    /* 018C  call base=L16 argc=1 nres=1 */
    0x000B1041,                /* 018E  move L11 <- L16 */
    0x00081046,                /* 018F  lconst L16 <- static#8   ("error") */
    0x00110097, 0x000B0010,    /* 0190  is L17 <- L11 is L16 */
    0x0001114B,                /* 0192  jf L17, +1 */
    0x000B0102,                /* 0193  return L11 count=1 */
    0x00100841,                /* 0194  move L16 <- L8 */
    0x00110A41,                /* 0195  move L17 <- L10 */
    0x001001A2, 0x00000001,    /* 0196  msg base=L16 argc=1 msg#0 nres=1   (append) */
    0x00100841,                /* 0198  move L16 <- L8 */
    0x00110B41,                /* 0199  move L17 <- L11 */
    0x001001A2, 0x00000001,    /* 019A  msg base=L16 argc=1 msg#0 nres=1   (append) */
    0xFFE0004F,                /* 019C  jmp -32 */
    0x00080102,                /* 019D  return L8 count=1 */
    0x00030F46,                /* 019E  lconst L15 <- static#3   ("list") */
    0x00100097, 0x8000000F,    /* 019F  is L16 <- P0 is L15 */
    0x001D104B,                /* 01A1  jf L16, +29 */
    0x00010F42,                /* 01A2  gget L15 <- g1   (head) */
    0x000A1042,                /* 01A3  gget L16 <- g10   (MAJOR_ARRAY) */
    0x000E1142,                /* 01A4  gget L17 <- g14   (len) */
    0x00128041,                /* 01A5  move L18 <- P0 */
    0x001101A0, 0x00010000,    /* 01A6  call base=L17 argc=1 nres=1 */
    0x000F02A0, 0x00010000,    /* 01A8  call base=L15 argc=2 nres=1 */
    0x000C0F41,                /* 01AA  move L12 <- L15 */
    0x000F00AE, 0x80000000,    /* 01AB  iter L15 <- P0 */
    0x000D00AF, 0x000F000F,    /* 01AD  itnext L13 <- L15, done +15 */
    0x00041042,                /* 01AF  gget L16 <- g4   (_value) */
    0x00110D41,                /* 01B0  move L17 <- L13 */
    0x001001A0, 0x00010000,    /* 01B1  call base=L16 argc=1 nres=1 */
    0x000E1041,                /* 01B3  move L14 <- L16 */
    0x00081046,                /* 01B4  lconst L16 <- static#8   ("error") */
    0x00110097, 0x000E0010,    /* 01B5  is L17 <- L14 is L16 */
    0x0001114B,                /* 01B7  jf L17, +1 */
    0x000E0102,                /* 01B8  return L14 count=1 */
    0x00100C41,                /* 01B9  move L16 <- L12 */
    0x00110E41,                /* 01BA  move L17 <- L14 */
    0x001001A2, 0x00000001,    /* 01BB  msg base=L16 argc=1 msg#0 nres=1   (append) */
    0xFFEF004F,                /* 01BD  jmp -17 */
    0x000C0102,                /* 01BE  return L12 count=1 */
    0x00010F42,                /* 01BF  gget L15 <- g1   (head) */
    0x00081042,                /* 01C0  gget L16 <- g8   (MAJOR_BYTES) */
    0x000E1142,                /* 01C1  gget L17 <- g14   (len) */
    0x00128041,                /* 01C2  move L18 <- P0 */
    0x001101A0, 0x00010000,    /* 01C3  call base=L17 argc=1 nres=1 */
    0x000F02A0, 0x00010000,    /* 01C5  call base=L15 argc=2 nres=1 */
    0x00000F41,                /* 01C7  move L0 <- L15 */
    0x000F0041,                /* 01C8  move L15 <- L0 */
    0x00108041,                /* 01C9  move L16 <- P0 */
    0x000F01A2, 0x00000001,    /* 01CA  msg base=L15 argc=1 msg#0 nres=1   (append) */
    0x00000102,                /* 01CC  return L0 count=1 */
};

const wy_module_image cbor_image = {
    .name = "cbor",
    .sections = {
        [WY_SEC_HEADER] = { cbor_header, sizeof cbor_header },
        [WY_SEC_STATICS] = { cbor_statics, sizeof cbor_statics },
        [WY_SEC_SYMBOLS] = { cbor_symbols, sizeof cbor_symbols },
        [WY_SEC_FUNCTIONS] = { cbor_functions, sizeof cbor_functions },
        [WY_SEC_CLASSES] = { cbor_classes, sizeof cbor_classes },
        [WY_SEC_MESSAGES] = { cbor_messages, sizeof cbor_messages },
        [WY_SEC_CODE] = { (const uint8_t*) cbor_code, sizeof cbor_code },
        [WY_SEC_EXPORTS] = { cbor_exports, sizeof cbor_exports },
        [WY_SEC_FREE] = { cbor_free, sizeof cbor_free },
    },
};
