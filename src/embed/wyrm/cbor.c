/* Generated from cbor by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

static const uint8_t cbor_header[] = {
    0xA3, 0x61, 0x6E, 0x64, 0x63, 0x62, 0x6F, 0x72, 0x61, 0x67, 0x0F, 0x61,
    0x6C, 0x04,
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
    0x6E, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x61, 0x6E, 0x61, 0x6C, 0x03,
    0x61, 0x63, 0x18, 0x22, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x64, 0x68,
    0x65, 0x61, 0x64, 0x61, 0x70, 0x82, 0xA1, 0x61, 0x6E, 0x65, 0x6D, 0x61,
    0x6A, 0x6F, 0x72, 0xA1, 0x61, 0x6E, 0x61, 0x6E, 0x61, 0x6C, 0x09, 0x61,
    0x63, 0x18, 0x3E, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x66, 0x65, 0x6E,
    0x63, 0x6F, 0x64, 0x65, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x65, 0x76,
    0x61, 0x6C, 0x75, 0x65, 0x61, 0x6C, 0x03, 0x61, 0x63, 0x18, 0xDE, 0x61,
    0x66, 0x00, 0xA5, 0x61, 0x6E, 0x64, 0x5F, 0x6B, 0x65, 0x79, 0x61, 0x70,
    0x81, 0xA1, 0x61, 0x6E, 0x63, 0x6B, 0x65, 0x79, 0x61, 0x6C, 0x03, 0x61,
    0x63, 0x18, 0xE3, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x66, 0x5F, 0x76,
    0x61, 0x6C, 0x75, 0x65, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x65, 0x76,
    0x61, 0x6C, 0x75, 0x65, 0x61, 0x6C, 0x15, 0x61, 0x63, 0x19, 0x01, 0x01,
    0x61, 0x66, 0x00,
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
    0x00020240,                /* 0000  i8 L2 <- 2 */
    0x00032040,                /* 0001  i8 L3 <- 32 */
    0x0001008A, 0x00020003,    /* 0002  pow L1 <- L2 ** L3 */
    0x00050143,                /* 0004  gset g5 <- L1   (U32_LIMIT) */
    0x00010040,                /* 0005  i8 L1 <- 0 */
    0x00060143,                /* 0006  gset g6 <- L1   (MAJOR_UINT) */
    0x00010140,                /* 0007  i8 L1 <- 1 */
    0x00070143,                /* 0008  gset g7 <- L1   (MAJOR_NEGINT) */
    0x00010240,                /* 0009  i8 L1 <- 2 */
    0x00080143,                /* 000A  gset g8 <- L1   (MAJOR_BYTES) */
    0x00010340,                /* 000B  i8 L1 <- 3 */
    0x00090143,                /* 000C  gset g9 <- L1   (MAJOR_TEXT) */
    0x00010440,                /* 000D  i8 L1 <- 4 */
    0x000A0143,                /* 000E  gset g10 <- L1   (MAJOR_ARRAY) */
    0x00010540,                /* 000F  i8 L1 <- 5 */
    0x000B0143,                /* 0010  gset g11 <- L1   (MAJOR_MAP) */
    0x000100A8, 0x00000000,    /* 0011  closure L1 <- fn#0, 0 caps */
    0x00000143,                /* 0013  gset g0 <- L1   (head_len) */
    0x000100A8, 0x00010000,    /* 0014  closure L1 <- fn#1, 0 caps */
    0x00010143,                /* 0016  gset g1 <- L1   (head) */
    0x000100A8, 0x00020000,    /* 0017  closure L1 <- fn#2, 0 caps */
    0x00020143,                /* 0019  gset g2 <- L1   (encode) */
    0x000100A8, 0x00030000,    /* 001A  closure L1 <- fn#3, 0 caps */
    0x00030143,                /* 001C  gset g3 <- L1   (_key) */
    0x000100A8, 0x00040000,    /* 001D  closure L1 <- fn#4, 0 caps */
    0x00040143,                /* 001F  gset g4 <- L1   (_value) */
    0x00000003,                /* 0020  lnil L0 */
    0x00000008,                /* 0021  return L0 */
    0x00011840,                /* 0022  i8 L1 <- 24 */
    0x00020091, 0x80000001,    /* 0023  lt L2 <- P0 < L1 */
    0x0002024B,                /* 0025  jf L2, +2 */
    0x00010140,                /* 0026  i8 L1 <- 1 */
    0x00010008,                /* 0027  return L1 */
    0x000100C0, 0x00000100,    /* 0028  i32 L1 <- 256 */
    0x00020091, 0x80000001,    /* 002A  lt L2 <- P0 < L1 */
    0x0002024B,                /* 002C  jf L2, +2 */
    0x00010240,                /* 002D  i8 L1 <- 2 */
    0x00010008,                /* 002E  return L1 */
    0x000100C0, 0x00010000,    /* 002F  i32 L1 <- 65536 */
    0x00020091, 0x80000001,    /* 0031  lt L2 <- P0 < L1 */
    0x0002024B,                /* 0033  jf L2, +2 */
    0x00010340,                /* 0034  i8 L1 <- 3 */
    0x00010008,                /* 0035  return L1 */
    0x00050142,                /* 0036  gget L1 <- g5   (U32_LIMIT) */
    0x00020091, 0x80000001,    /* 0037  lt L2 <- P0 < L1 */
    0x0002024B,                /* 0039  jf L2, +2 */
    0x00010540,                /* 003A  i8 L1 <- 5 */
    0x00010008,                /* 003B  return L1 */
    0x00010940,                /* 003C  i8 L1 <- 9 */
    0x00010008,                /* 003D  return L1 */
    0x000C0442,                /* 003E  gget L4 <- g12   (bytes) */
    0x00050040,                /* 003F  i8 L5 <- 0 */
    0x00040151,                /* 0040  call base=L4 argc=1 */
    0x00040041,                /* 0041  move L4 <- L0 */
    0x00010441,                /* 0042  move L1 <- L4 */
    0x00042040,                /* 0043  i8 L4 <- 32 */
    0x00020087, 0x80000004,    /* 0044  mul L2 <- P0 * L4 */
    0x00041840,                /* 0046  i8 L4 <- 24 */
    0x00050091, 0x80010004,    /* 0047  lt L5 <- P1 < L4 */
    0x0007054B,                /* 0049  jf L5, +7 */
    0x00040141,                /* 004A  move L4 <- L1 */
    0x00050085, 0x00028001,    /* 004B  add L5 <- L2 + P1 */
    0x000401B7, 0x00000000,    /* 004D  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 004F  move L4 <- L0 */
    0x008C004F,                /* 0050  jmp +140 */
    0x000400C0, 0x00000100,    /* 0051  i32 L4 <- 256 */
    0x00050091, 0x80010004,    /* 0053  lt L5 <- P1 < L4 */
    0x000E054B,                /* 0055  jf L5, +14 */
    0x00040141,                /* 0056  move L4 <- L1 */
    0x00051840,                /* 0057  i8 L5 <- 24 */
    0x00060085, 0x00020005,    /* 0058  add L6 <- L2 + L5 */
    0x00050641,                /* 005A  move L5 <- L6 */
    0x000401B7, 0x00000000,    /* 005B  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 005D  move L4 <- L0 */
    0x00040141,                /* 005E  move L4 <- L1 */
    0x00058141,                /* 005F  move L5 <- P1 */
    0x000401B7, 0x00000000,    /* 0060  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 0062  move L4 <- L0 */
    0x0079004F,                /* 0063  jmp +121 */
    0x000400C0, 0x00010000,    /* 0064  i32 L4 <- 65536 */
    0x00050091, 0x80010004,    /* 0066  lt L5 <- P1 < L4 */
    0x001E054B,                /* 0068  jf L5, +30 */
    0x00040141,                /* 0069  move L4 <- L1 */
    0x00051940,                /* 006A  i8 L5 <- 25 */
    0x00060085, 0x00020005,    /* 006B  add L6 <- L2 + L5 */
    0x00050641,                /* 006D  move L5 <- L6 */
    0x000401B7, 0x00000000,    /* 006E  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 0070  move L4 <- L0 */
    0x00040141,                /* 0071  move L4 <- L1 */
    0x00050840,                /* 0072  i8 L5 <- 8 */
    0x0006008E, 0x80010005,    /* 0073  shr L6 <- P1 >> L5 */
    0x000700C0, 0x000000FF,    /* 0075  i32 L7 <- 255 */
    0x0008008B, 0x00060007,    /* 0077  band L8 <- L6 & L7 */
    0x00050841,                /* 0079  move L5 <- L8 */
    0x000401B7, 0x00000000,    /* 007A  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 007C  move L4 <- L0 */
    0x00040141,                /* 007D  move L4 <- L1 */
    0x000500C0, 0x000000FF,    /* 007E  i32 L5 <- 255 */
    0x0006008B, 0x80010005,    /* 0080  band L6 <- P1 & L5 */
    0x00050641,                /* 0082  move L5 <- L6 */
    0x000401B7, 0x00000000,    /* 0083  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 0085  move L4 <- L0 */
    0x0056004F,                /* 0086  jmp +86 */
    0x00050442,                /* 0087  gget L4 <- g5   (U32_LIMIT) */
    0x00050091, 0x80010004,    /* 0088  lt L5 <- P1 < L4 */
    0x0036054B,                /* 008A  jf L5, +54 */
    0x00040141,                /* 008B  move L4 <- L1 */
    0x00051A40,                /* 008C  i8 L5 <- 26 */
    0x00060085, 0x00020005,    /* 008D  add L6 <- L2 + L5 */
    0x00050641,                /* 008F  move L5 <- L6 */
    0x000401B7, 0x00000000,    /* 0090  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 0092  move L4 <- L0 */
    0x00040141,                /* 0093  move L4 <- L1 */
    0x00051840,                /* 0094  i8 L5 <- 24 */
    0x0006008E, 0x80010005,    /* 0095  shr L6 <- P1 >> L5 */
    0x000700C0, 0x000000FF,    /* 0097  i32 L7 <- 255 */
    0x0008008B, 0x00060007,    /* 0099  band L8 <- L6 & L7 */
    0x00050841,                /* 009B  move L5 <- L8 */
    0x000401B7, 0x00000000,    /* 009C  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 009E  move L4 <- L0 */
    0x00040141,                /* 009F  move L4 <- L1 */
    0x00051040,                /* 00A0  i8 L5 <- 16 */
    0x0006008E, 0x80010005,    /* 00A1  shr L6 <- P1 >> L5 */
    0x000700C0, 0x000000FF,    /* 00A3  i32 L7 <- 255 */
    0x0008008B, 0x00060007,    /* 00A5  band L8 <- L6 & L7 */
    0x00050841,                /* 00A7  move L5 <- L8 */
    0x000401B7, 0x00000000,    /* 00A8  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 00AA  move L4 <- L0 */
    0x00040141,                /* 00AB  move L4 <- L1 */
    0x00050840,                /* 00AC  i8 L5 <- 8 */
    0x0006008E, 0x80010005,    /* 00AD  shr L6 <- P1 >> L5 */
    0x000700C0, 0x000000FF,    /* 00AF  i32 L7 <- 255 */
    0x0008008B, 0x00060007,    /* 00B1  band L8 <- L6 & L7 */
    0x00050841,                /* 00B3  move L5 <- L8 */
    0x000401B7, 0x00000000,    /* 00B4  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 00B6  move L4 <- L0 */
    0x00040141,                /* 00B7  move L4 <- L1 */
    0x000500C0, 0x000000FF,    /* 00B8  i32 L5 <- 255 */
    0x0006008B, 0x80010005,    /* 00BA  band L6 <- P1 & L5 */
    0x00050641,                /* 00BC  move L5 <- L6 */
    0x000401B7, 0x00000000,    /* 00BD  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 00BF  move L4 <- L0 */
    0x001C004F,                /* 00C0  jmp +28 */
    0x00040141,                /* 00C1  move L4 <- L1 */
    0x00051B40,                /* 00C2  i8 L5 <- 27 */
    0x00060085, 0x00020005,    /* 00C3  add L6 <- L2 + L5 */
    0x00050641,                /* 00C5  move L5 <- L6 */
    0x000401B7, 0x00000000,    /* 00C6  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 00C8  move L4 <- L0 */
    0x00033840,                /* 00C9  i8 L3 <- 56 */
    0x00040040,                /* 00CA  i8 L4 <- 0 */
    0x00050094, 0x00030004,    /* 00CB  ge L5 <- L3 >= L4 */
    0x000F054B,                /* 00CD  jf L5, +15 */
    0x00040141,                /* 00CE  move L4 <- L1 */
    0x0005008E, 0x80010003,    /* 00CF  shr L5 <- P1 >> L3 */
    0x000600C0, 0x000000FF,    /* 00D1  i32 L6 <- 255 */
    0x0007008B, 0x00050006,    /* 00D3  band L7 <- L5 & L6 */
    0x00050741,                /* 00D5  move L5 <- L7 */
    0x000401B7, 0x00000000,    /* 00D6  msg base=L4 argc=1 msg#0   (append) */
    0x00040041,                /* 00D8  move L4 <- L0 */
    0x00040840,                /* 00D9  i8 L4 <- 8 */
    0x00030086, 0x00030004,    /* 00DA  sub L3 <- L3 - L4 */
    0xFFED004F,                /* 00DC  jmp -19 */
    0x00010008,                /* 00DD  return L1 */
    0x00040142,                /* 00DE  gget L1 <- g4   (_value) */
    0x00028041,                /* 00DF  move L2 <- P0 */
    0x00010151,                /* 00E0  call base=L1 argc=1 */
    0x00010041,                /* 00E1  move L1 <- L0 */
    0x00010008,                /* 00E2  return L1 */
    0x00000246,                /* 00E3  lconst L2 <- static#0   ("nil") */
    0x00010097, 0x80000002,    /* 00E4  is L1 <- P0 is L2 */
    0x000F014C,                /* 00E6  jt L1, +15 */
    0x00010246,                /* 00E7  lconst L2 <- static#1   ("bool") */
    0x00010097, 0x80000002,    /* 00E8  is L1 <- P0 is L2 */
    0x000B014C,                /* 00EA  jt L1, +11 */
    0x00020246,                /* 00EB  lconst L2 <- static#2   ("float") */
    0x00010097, 0x80000002,    /* 00EC  is L1 <- P0 is L2 */
    0x0007014C,                /* 00EE  jt L1, +7 */
    0x00030246,                /* 00EF  lconst L2 <- static#3   ("list") */
    0x00010097, 0x80000002,    /* 00F0  is L1 <- P0 is L2 */
    0x0003014C,                /* 00F2  jt L1, +3 */
    0x00040246,                /* 00F3  lconst L2 <- static#4   ("dict") */
    0x00010097, 0x80000002,    /* 00F4  is L1 <- P0 is L2 */
    0x0005014B,                /* 00F6  jf L1, +5 */
    0x000D0142,                /* 00F7  gget L1 <- g13   (error) */
    0x00050246,                /* 00F8  lconst L2 <- static#5   ("cbor: a map key must be an int, a str or bytes") */
    0x00010151,                /* 00F9  call base=L1 argc=1 */
    0x00010041,                /* 00FA  move L1 <- L0 */
    0x00010008,                /* 00FB  return L1 */
    0x00040142,                /* 00FC  gget L1 <- g4   (_value) */
    0x00028041,                /* 00FD  move L2 <- P0 */
    0x00010151,                /* 00FE  call base=L1 argc=1 */
    0x00010041,                /* 00FF  move L1 <- L0 */
    0x00010008,                /* 0100  return L1 */
    0x00001046,                /* 0101  lconst L16 <- static#0   ("nil") */
    0x00110097, 0x80000010,    /* 0102  is L17 <- P0 is L16 */
    0x000C114B,                /* 0104  jf L17, +12 */
    0x000C1042,                /* 0105  gget L16 <- g12   (bytes) */
    0x00110040,                /* 0106  i8 L17 <- 0 */
    0x00100151,                /* 0107  call base=L16 argc=1 */
    0x00100041,                /* 0108  move L16 <- L0 */
    0x00021041,                /* 0109  move L2 <- L16 */
    0x00100241,                /* 010A  move L16 <- L2 */
    0x001100C0, 0x000000F6,    /* 010B  i32 L17 <- 246 */
    0x001001B7, 0x00000000,    /* 010D  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 010F  move L16 <- L0 */
    0x00020008,                /* 0110  return L2 */
    0x00011046,                /* 0111  lconst L16 <- static#1   ("bool") */
    0x00110097, 0x80000010,    /* 0112  is L17 <- P0 is L16 */
    0x0014114B,                /* 0114  jf L17, +20 */
    0x000C1042,                /* 0115  gget L16 <- g12   (bytes) */
    0x00110040,                /* 0116  i8 L17 <- 0 */
    0x00100151,                /* 0117  call base=L16 argc=1 */
    0x00100041,                /* 0118  move L16 <- L0 */
    0x00031041,                /* 0119  move L3 <- L16 */
    0x0007804B,                /* 011A  jf P0, +7 */
    0x00100341,                /* 011B  move L16 <- L3 */
    0x001100C0, 0x000000F5,    /* 011C  i32 L17 <- 245 */
    0x001001B7, 0x00000000,    /* 011E  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 0120  move L16 <- L0 */
    0x0006004F,                /* 0121  jmp +6 */
    0x00100341,                /* 0122  move L16 <- L3 */
    0x001100C0, 0x000000F4,    /* 0123  i32 L17 <- 244 */
    0x001001B7, 0x00000000,    /* 0125  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 0127  move L16 <- L0 */
    0x00030008,                /* 0128  return L3 */
    0x00061046,                /* 0129  lconst L16 <- static#6   ("int") */
    0x00110097, 0x80000010,    /* 012A  is L17 <- P0 is L16 */
    0x0014114B,                /* 012C  jf L17, +20 */
    0x00100040,                /* 012D  i8 L16 <- 0 */
    0x00110094, 0x80000010,    /* 012E  ge L17 <- P0 >= L16 */
    0x0006114B,                /* 0130  jf L17, +6 */
    0x00011042,                /* 0131  gget L16 <- g1   (head) */
    0x00061142,                /* 0132  gget L17 <- g6   (MAJOR_UINT) */
    0x00128041,                /* 0133  move L18 <- P0 */
    0x00100251,                /* 0134  call base=L16 argc=2 */
    0x00100041,                /* 0135  move L16 <- L0 */
    0x00100008,                /* 0136  return L16 */
    0x00011042,                /* 0137  gget L16 <- g1   (head) */
    0x00071142,                /* 0138  gget L17 <- g7   (MAJOR_NEGINT) */
    0x00120140,                /* 0139  i8 L18 <- 1 */
    0x00131248,                /* 013A  neg L19 <- L18 */
    0x00140086, 0x00138000,    /* 013B  sub L20 <- L19 - P0 */
    0x00121441,                /* 013D  move L18 <- L20 */
    0x00100251,                /* 013E  call base=L16 argc=2 */
    0x00100041,                /* 013F  move L16 <- L0 */
    0x00100008,                /* 0140  return L16 */
    0x00021046,                /* 0141  lconst L16 <- static#2   ("float") */
    0x00110097, 0x80000010,    /* 0142  is L17 <- P0 is L16 */
    0x0026114B,                /* 0144  jf L17, +38 */
    0x000C1042,                /* 0145  gget L16 <- g12   (bytes) */
    0x00110840,                /* 0146  i8 L17 <- 8 */
    0x00100151,                /* 0147  call base=L16 argc=1 */
    0x00100041,                /* 0148  move L16 <- L0 */
    0x00041041,                /* 0149  move L4 <- L16 */
    0x00100441,                /* 014A  move L16 <- L4 */
    0x00110040,                /* 014B  i8 L17 <- 0 */
    0x00128041,                /* 014C  move L18 <- P0 */
    0x001002B7, 0x00010000,    /* 014D  msg base=L16 argc=2 msg#1   (pack_f64) */
    0x00100041,                /* 014F  move L16 <- L0 */
    0x000C1042,                /* 0150  gget L16 <- g12   (bytes) */
    0x00110040,                /* 0151  i8 L17 <- 0 */
    0x00100151,                /* 0152  call base=L16 argc=1 */
    0x00100041,                /* 0153  move L16 <- L0 */
    0x00051041,                /* 0154  move L5 <- L16 */
    0x00100541,                /* 0155  move L16 <- L5 */
    0x001100C0, 0x000000FB,    /* 0156  i32 L17 <- 251 */
    0x001001B7, 0x00000000,    /* 0158  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 015A  move L16 <- L0 */
    0x00060740,                /* 015B  i8 L6 <- 7 */
    0x00100040,                /* 015C  i8 L16 <- 0 */
    0x00110094, 0x00060010,    /* 015D  ge L17 <- L6 >= L16 */
    0x000A114B,                /* 015F  jf L17, +10 */
    0x00100541,                /* 0160  move L16 <- L5 */
    0x00110098, 0x00040006,    /* 0161  getidx L17 <- L4[L6] */
    0x001001B7, 0x00000000,    /* 0163  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 0165  move L16 <- L0 */
    0x00100140,                /* 0166  i8 L16 <- 1 */
    0x00060086, 0x00060010,    /* 0167  sub L6 <- L6 - L16 */
    0xFFF2004F,                /* 0169  jmp -14 */
    0x00050008,                /* 016A  return L5 */
    0x00071046,                /* 016B  lconst L16 <- static#7   ("str") */
    0x00110097, 0x80000010,    /* 016C  is L17 <- P0 is L16 */
    0x0014114B,                /* 016E  jf L17, +20 */
    0x000C1042,                /* 016F  gget L16 <- g12   (bytes) */
    0x00118041,                /* 0170  move L17 <- P0 */
    0x00100151,                /* 0171  call base=L16 argc=1 */
    0x00100041,                /* 0172  move L16 <- L0 */
    0x00071041,                /* 0173  move L7 <- L16 */
    0x00011042,                /* 0174  gget L16 <- g1   (head) */
    0x00091142,                /* 0175  gget L17 <- g9   (MAJOR_TEXT) */
    0x000E1242,                /* 0176  gget L18 <- g14   (len) */
    0x00130741,                /* 0177  move L19 <- L7 */
    0x00120151,                /* 0178  call base=L18 argc=1 */
    0x00120041,                /* 0179  move L18 <- L0 */
    0x00100251,                /* 017A  call base=L16 argc=2 */
    0x00100041,                /* 017B  move L16 <- L0 */
    0x00081041,                /* 017C  move L8 <- L16 */
    0x00100841,                /* 017D  move L16 <- L8 */
    0x00110741,                /* 017E  move L17 <- L7 */
    0x001001B7, 0x00000000,    /* 017F  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 0181  move L16 <- L0 */
    0x00080008,                /* 0182  return L8 */
    0x00041046,                /* 0183  lconst L16 <- static#4   ("dict") */
    0x00110097, 0x80000010,    /* 0184  is L17 <- P0 is L16 */
    0x002E114B,                /* 0186  jf L17, +46 */
    0x00011042,                /* 0187  gget L16 <- g1   (head) */
    0x000B1142,                /* 0188  gget L17 <- g11   (MAJOR_MAP) */
    0x000E1242,                /* 0189  gget L18 <- g14   (len) */
    0x00138041,                /* 018A  move L19 <- P0 */
    0x00120151,                /* 018B  call base=L18 argc=1 */
    0x00120041,                /* 018C  move L18 <- L0 */
    0x00100251,                /* 018D  call base=L16 argc=2 */
    0x00100041,                /* 018E  move L16 <- L0 */
    0x00091041,                /* 018F  move L9 <- L16 */
    0x001000AE, 0x80000000,    /* 0190  iter L16 <- P0 */
    0x000A00AF, 0x00100020,    /* 0192  itnext L10 <- L16, done +32 */
    0x00031142,                /* 0194  gget L17 <- g3   (_key) */
    0x00120A41,                /* 0195  move L18 <- L10 */
    0x00110151,                /* 0196  call base=L17 argc=1 */
    0x00110041,                /* 0197  move L17 <- L0 */
    0x000B1141,                /* 0198  move L11 <- L17 */
    0x00081146,                /* 0199  lconst L17 <- static#8   ("error") */
    0x00120097, 0x000B0011,    /* 019A  is L18 <- L11 is L17 */
    0x0001124B,                /* 019C  jf L18, +1 */
    0x000B0008,                /* 019D  return L11 */
    0x00041142,                /* 019E  gget L17 <- g4   (_value) */
    0x00120098, 0x8000000A,    /* 019F  getidx L18 <- P0[L10] */
    0x00110151,                /* 01A1  call base=L17 argc=1 */
    0x00110041,                /* 01A2  move L17 <- L0 */
    0x000C1141,                /* 01A3  move L12 <- L17 */
    0x00081146,                /* 01A4  lconst L17 <- static#8   ("error") */
    0x00120097, 0x000C0011,    /* 01A5  is L18 <- L12 is L17 */
    0x0001124B,                /* 01A7  jf L18, +1 */
    0x000C0008,                /* 01A8  return L12 */
    0x00110941,                /* 01A9  move L17 <- L9 */
    0x00120B41,                /* 01AA  move L18 <- L11 */
    0x001101B7, 0x00000000,    /* 01AB  msg base=L17 argc=1 msg#0   (append) */
    0x00110041,                /* 01AD  move L17 <- L0 */
    0x00110941,                /* 01AE  move L17 <- L9 */
    0x00120C41,                /* 01AF  move L18 <- L12 */
    0x001101B7, 0x00000000,    /* 01B0  msg base=L17 argc=1 msg#0   (append) */
    0x00110041,                /* 01B2  move L17 <- L0 */
    0xFFDE004F,                /* 01B3  jmp -34 */
    0x00090008,                /* 01B4  return L9 */
    0x00031046,                /* 01B5  lconst L16 <- static#3   ("list") */
    0x00110097, 0x80000010,    /* 01B6  is L17 <- P0 is L16 */
    0x001E114B,                /* 01B8  jf L17, +30 */
    0x00011042,                /* 01B9  gget L16 <- g1   (head) */
    0x000A1142,                /* 01BA  gget L17 <- g10   (MAJOR_ARRAY) */
    0x000E1242,                /* 01BB  gget L18 <- g14   (len) */
    0x00138041,                /* 01BC  move L19 <- P0 */
    0x00120151,                /* 01BD  call base=L18 argc=1 */
    0x00120041,                /* 01BE  move L18 <- L0 */
    0x00100251,                /* 01BF  call base=L16 argc=2 */
    0x00100041,                /* 01C0  move L16 <- L0 */
    0x000D1041,                /* 01C1  move L13 <- L16 */
    0x001000AE, 0x80000000,    /* 01C2  iter L16 <- P0 */
    0x000E00AF, 0x00100010,    /* 01C4  itnext L14 <- L16, done +16 */
    0x00041142,                /* 01C6  gget L17 <- g4   (_value) */
    0x00120E41,                /* 01C7  move L18 <- L14 */
    0x00110151,                /* 01C8  call base=L17 argc=1 */
    0x00110041,                /* 01C9  move L17 <- L0 */
    0x000F1141,                /* 01CA  move L15 <- L17 */
    0x00081146,                /* 01CB  lconst L17 <- static#8   ("error") */
    0x00120097, 0x000F0011,    /* 01CC  is L18 <- L15 is L17 */
    0x0001124B,                /* 01CE  jf L18, +1 */
    0x000F0008,                /* 01CF  return L15 */
    0x00110D41,                /* 01D0  move L17 <- L13 */
    0x00120F41,                /* 01D1  move L18 <- L15 */
    0x001101B7, 0x00000000,    /* 01D2  msg base=L17 argc=1 msg#0   (append) */
    0x00110041,                /* 01D4  move L17 <- L0 */
    0xFFEE004F,                /* 01D5  jmp -18 */
    0x000D0008,                /* 01D6  return L13 */
    0x00011042,                /* 01D7  gget L16 <- g1   (head) */
    0x00081142,                /* 01D8  gget L17 <- g8   (MAJOR_BYTES) */
    0x000E1242,                /* 01D9  gget L18 <- g14   (len) */
    0x00138041,                /* 01DA  move L19 <- P0 */
    0x00120151,                /* 01DB  call base=L18 argc=1 */
    0x00120041,                /* 01DC  move L18 <- L0 */
    0x00100251,                /* 01DD  call base=L16 argc=2 */
    0x00100041,                /* 01DE  move L16 <- L0 */
    0x00011041,                /* 01DF  move L1 <- L16 */
    0x00100141,                /* 01E0  move L16 <- L1 */
    0x00118041,                /* 01E1  move L17 <- P0 */
    0x001001B7, 0x00000000,    /* 01E2  msg base=L16 argc=1 msg#0   (append) */
    0x00100041,                /* 01E4  move L16 <- L0 */
    0x00010008,                /* 01E5  return L1 */
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
