/* Generated from sexp_print by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

static const uint8_t sexp_print_header[] = {
    0xA3, 0x61, 0x6E, 0x6A, 0x73, 0x65, 0x78, 0x70, 0x5F, 0x70, 0x72, 0x69,
    0x6E, 0x74, 0x61, 0x67, 0x0F, 0x61, 0x6C, 0x04,
};

static const uint8_t sexp_print_statics[] = {
    0x98, 0x1A, 0x70, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x61, 0x78, 0x61, 0x6E, 0x61,
    0x72, 0x61, 0x74, 0x63, 0x6E, 0x69, 0x6C, 0x60, 0x61, 0x2D, 0x61, 0x20,
    0x62, 0x28, 0x29, 0x64, 0x70, 0x61, 0x69, 0x72, 0x61, 0x28, 0x63, 0x20,
    0x2E, 0x20, 0x61, 0x29, 0x63, 0x73, 0x79, 0x6D, 0x63, 0x73, 0x74, 0x72,
    0x64, 0x62, 0x6F, 0x6F, 0x6C, 0x67, 0x23, 0x3C, 0x74, 0x72, 0x75, 0x65,
    0x3E, 0x68, 0x23, 0x3C, 0x66, 0x61, 0x6C, 0x73, 0x65, 0x3E, 0x63, 0x69,
    0x6E, 0x74, 0x65, 0x66, 0x6C, 0x6F, 0x61, 0x74, 0x64, 0x6C, 0x69, 0x73,
    0x74, 0x68, 0x23, 0x6E, 0x61, 0x74, 0x69, 0x76, 0x65, 0x28, 0x65, 0x74,
    0x75, 0x70, 0x6C, 0x65, 0x67, 0x23, 0x74, 0x75, 0x70, 0x6C, 0x65, 0x28,
    0x69, 0x23, 0x3C, 0x6F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x3E,
};

static const uint8_t sexp_print_symbols[] = {
    0x83, 0x67, 0x70, 0x61, 0x63, 0x6B, 0x5F, 0x75, 0x38, 0x66, 0x74, 0x6F,
    0x5F, 0x73, 0x74, 0x72, 0x66, 0x73, 0x75, 0x62, 0x73, 0x74, 0x72,
};

static const uint8_t sexp_print_functions[] = {
    0x87, 0xA5, 0x61, 0x6E, 0x69, 0x5F, 0x73, 0x70, 0x5F, 0x61, 0x73, 0x63,
    0x69, 0x69, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x64, 0x63, 0x6F, 0x64,
    0x65, 0x61, 0x6C, 0x05, 0x61, 0x63, 0x18, 0x25, 0x61, 0x66, 0x00, 0xA5,
    0x61, 0x6E, 0x6E, 0x5F, 0x73, 0x70, 0x5F, 0x68, 0x65, 0x78, 0x5F, 0x65,
    0x73, 0x63, 0x61, 0x70, 0x65, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x64,
    0x63, 0x6F, 0x64, 0x65, 0x61, 0x6C, 0x09, 0x61, 0x63, 0x18, 0x35, 0x61,
    0x66, 0x00, 0xA5, 0x61, 0x6E, 0x65, 0x71, 0x75, 0x6F, 0x74, 0x65, 0x61,
    0x70, 0x81, 0xA1, 0x61, 0x6E, 0x64, 0x74, 0x65, 0x78, 0x74, 0x61, 0x6C,
    0x0A, 0x61, 0x63, 0x18, 0x50, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E, 0x6B,
    0x73, 0x70, 0x65, 0x6C, 0x6C, 0x5F, 0x66, 0x6C, 0x6F, 0x61, 0x74, 0x61,
    0x70, 0x81, 0xA1, 0x61, 0x6E, 0x65, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x61,
    0x6C, 0x0B, 0x61, 0x63, 0x18, 0xC3, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E,
    0x6C, 0x5F, 0x73, 0x70, 0x5F, 0x65, 0x78, 0x70, 0x6F, 0x6E, 0x65, 0x6E,
    0x74, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E, 0x63, 0x65, 0x78, 0x70, 0x61,
    0x6C, 0x08, 0x61, 0x63, 0x18, 0xF9, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E,
    0x69, 0x5F, 0x73, 0x70, 0x5F, 0x69, 0x74, 0x65, 0x6D, 0x73, 0x61, 0x70,
    0x81, 0xA1, 0x61, 0x6E, 0x65, 0x69, 0x74, 0x65, 0x6D, 0x73, 0x61, 0x6C,
    0x07, 0x61, 0x63, 0x19, 0x01, 0x2C, 0x61, 0x66, 0x00, 0xA5, 0x61, 0x6E,
    0x65, 0x77, 0x72, 0x69, 0x74, 0x65, 0x61, 0x70, 0x81, 0xA1, 0x61, 0x6E,
    0x65, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x61, 0x6C, 0x09, 0x61, 0x63, 0x19,
    0x01, 0x40, 0x61, 0x66, 0x00,
};

static const uint8_t sexp_print_classes[] = {
    0x80,
};

static const uint8_t sexp_print_messages[] = {
    0x83, 0xA1, 0x61, 0x70, 0x81, 0x00, 0xA1, 0x61, 0x70, 0x81, 0x01, 0xA1,
    0x61, 0x70, 0x81, 0x02,
};

static const uint8_t sexp_print_exports[] = {
    0xAA, 0x69, 0x5F, 0x73, 0x70, 0x5F, 0x61, 0x73, 0x63, 0x69, 0x69, 0x00,
    0x6E, 0x5F, 0x73, 0x70, 0x5F, 0x68, 0x65, 0x78, 0x5F, 0x65, 0x73, 0x63,
    0x61, 0x70, 0x65, 0x01, 0x65, 0x71, 0x75, 0x6F, 0x74, 0x65, 0x02, 0x6B,
    0x73, 0x70, 0x65, 0x6C, 0x6C, 0x5F, 0x66, 0x6C, 0x6F, 0x61, 0x74, 0x03,
    0x6C, 0x5F, 0x73, 0x70, 0x5F, 0x65, 0x78, 0x70, 0x6F, 0x6E, 0x65, 0x6E,
    0x74, 0x04, 0x69, 0x5F, 0x73, 0x70, 0x5F, 0x69, 0x74, 0x65, 0x6D, 0x73,
    0x05, 0x65, 0x77, 0x72, 0x69, 0x74, 0x65, 0x06, 0x6D, 0x5F, 0x53, 0x50,
    0x5F, 0x42, 0x41, 0x43, 0x4B, 0x53, 0x4C, 0x41, 0x53, 0x48, 0x07, 0x69,
    0x5F, 0x53, 0x50, 0x5F, 0x51, 0x55, 0x4F, 0x54, 0x45, 0x08, 0x67, 0x5F,
    0x53, 0x50, 0x5F, 0x48, 0x45, 0x58, 0x09,
};

static const uint8_t sexp_print_free[] = {
    0xA5, 0x65, 0x62, 0x79, 0x74, 0x65, 0x73, 0x0A, 0x63, 0x6C, 0x65, 0x6E,
    0x0B, 0x63, 0x73, 0x74, 0x72, 0x0C, 0x63, 0x63, 0x61, 0x72, 0x0D, 0x63,
    0x63, 0x64, 0x72, 0x0E,
};

static const uint32_t sexp_print_code[] = {
    0x000100A8, 0x00000000,    /* 0000  closure L1 <- fn#0, 0 caps */
    0x00000143,                /* 0002  gset g0 <- L1   (_sp_ascii) */
    0x00000242,                /* 0003  gget L2 <- g0   (_sp_ascii) */
    0x00035C40,                /* 0004  i8 L3 <- 92 */
    0x00020151,                /* 0005  call base=L2 argc=1 */
    0x00020041,                /* 0006  move L2 <- L0 */
    0x00010241,                /* 0007  move L1 <- L2 */
    0x00070143,                /* 0008  gset g7 <- L1   (_SP_BACKSLASH) */
    0x00000242,                /* 0009  gget L2 <- g0   (_sp_ascii) */
    0x00032240,                /* 000A  i8 L3 <- 34 */
    0x00020151,                /* 000B  call base=L2 argc=1 */
    0x00020041,                /* 000C  move L2 <- L0 */
    0x00010241,                /* 000D  move L1 <- L2 */
    0x00080143,                /* 000E  gset g8 <- L1   (_SP_QUOTE) */
    0x00000146,                /* 000F  lconst L1 <- static#0   ("0123456789ABCDEF") */
    0x00090143,                /* 0010  gset g9 <- L1   (_SP_HEX) */
    0x000100A8, 0x00010000,    /* 0011  closure L1 <- fn#1, 0 caps */
    0x00010143,                /* 0013  gset g1 <- L1   (_sp_hex_escape) */
    0x000100A8, 0x00020000,    /* 0014  closure L1 <- fn#2, 0 caps */
    0x00020143,                /* 0016  gset g2 <- L1   (quote) */
    0x000100A8, 0x00030000,    /* 0017  closure L1 <- fn#3, 0 caps */
    0x00030143,                /* 0019  gset g3 <- L1   (spell_float) */
    0x000100A8, 0x00040000,    /* 001A  closure L1 <- fn#4, 0 caps */
    0x00040143,                /* 001C  gset g4 <- L1   (_sp_exponent) */
    0x000100A8, 0x00050000,    /* 001D  closure L1 <- fn#5, 0 caps */
    0x00050143,                /* 001F  gset g5 <- L1   (_sp_items) */
    0x000100A8, 0x00060000,    /* 0020  closure L1 <- fn#6, 0 caps */
    0x00060143,                /* 0022  gset g6 <- L1   (write) */
    0x00000003,                /* 0023  lnil L0 */
    0x00000008,                /* 0024  return L0 */
    0x000A0242,                /* 0025  gget L2 <- g10   (bytes) */
    0x00030140,                /* 0026  i8 L3 <- 1 */
    0x00020151,                /* 0027  call base=L2 argc=1 */
    0x00020041,                /* 0028  move L2 <- L0 */
    0x00010241,                /* 0029  move L1 <- L2 */
    0x00020141,                /* 002A  move L2 <- L1 */
    0x00030040,                /* 002B  i8 L3 <- 0 */
    0x00048041,                /* 002C  move L4 <- P0 */
    0x000202B7, 0x00000000,    /* 002D  msg base=L2 argc=2 msg#0   (pack_u8) */
    0x00020041,                /* 002F  move L2 <- L0 */
    0x00020141,                /* 0030  move L2 <- L1 */
    0x000200B7, 0x00010000,    /* 0031  msg base=L2 argc=0 msg#1   (to_str) */
    0x00020041,                /* 0033  move L2 <- L0 */
    0x00020008,                /* 0034  return L2 */
    0x00070142,                /* 0035  gget L1 <- g7   (_SP_BACKSLASH) */
    0x00010246,                /* 0036  lconst L2 <- static#1   ("x") */
    0x00030085, 0x00010002,    /* 0037  add L3 <- L1 + L2 */
    0x00090442,                /* 0039  gget L4 <- g9   (_SP_HEX) */
    0x00050440,                /* 003A  i8 L5 <- 4 */
    0x0006008E, 0x80000005,    /* 003B  shr L6 <- P0 >> L5 */
    0x00050641,                /* 003D  move L5 <- L6 */
    0x00060140,                /* 003E  i8 L6 <- 1 */
    0x000402B7, 0x00020000,    /* 003F  msg base=L4 argc=2 msg#2   (substr) */
    0x00040041,                /* 0041  move L4 <- L0 */
    0x00050085, 0x00030004,    /* 0042  add L5 <- L3 + L4 */
    0x00090642,                /* 0044  gget L6 <- g9   (_SP_HEX) */
    0x00070F40,                /* 0045  i8 L7 <- 15 */
    0x0008008B, 0x80000007,    /* 0046  band L8 <- P0 & L7 */
    0x00070841,                /* 0048  move L7 <- L8 */
    0x00080140,                /* 0049  i8 L8 <- 1 */
    0x000602B7, 0x00020000,    /* 004A  msg base=L6 argc=2 msg#2   (substr) */
    0x00060041,                /* 004C  move L6 <- L0 */
    0x00070085, 0x00050006,    /* 004D  add L7 <- L5 + L6 */
    0x00070008,                /* 004F  return L7 */
    0x00080142,                /* 0050  gget L1 <- g8   (_SP_QUOTE) */
    0x000B0742,                /* 0051  gget L7 <- g11   (len) */
    0x00088041,                /* 0052  move L8 <- P0 */
    0x00070151,                /* 0053  call base=L7 argc=1 */
    0x00070041,                /* 0054  move L7 <- L0 */
    0x00020741,                /* 0055  move L2 <- L7 */
    0x00030040,                /* 0056  i8 L3 <- 0 */
    0x00040040,                /* 0057  i8 L4 <- 0 */
    0x00070091, 0x00040002,    /* 0058  lt L7 <- L4 < L2 */
    0x0058074B,                /* 005A  jf L7, +88 */
    0x00050098, 0x80000004,    /* 005B  getidx L5 <- P0[L4] */
    0x00060003,                /* 005D  lnil L6 */
    0x00075C40,                /* 005E  i8 L7 <- 92 */
    0x0008008F, 0x00050007,    /* 005F  eq L8 <- L5 == L7 */
    0x0005084B,                /* 0061  jf L8, +5 */
    0x00070742,                /* 0062  gget L7 <- g7   (_SP_BACKSLASH) */
    0x00070842,                /* 0063  gget L8 <- g7   (_SP_BACKSLASH) */
    0x00060085, 0x00070008,    /* 0064  add L6 <- L7 + L8 */
    0x0031004F,                /* 0066  jmp +49 */
    0x00072240,                /* 0067  i8 L7 <- 34 */
    0x0008008F, 0x00050007,    /* 0068  eq L8 <- L5 == L7 */
    0x0005084B,                /* 006A  jf L8, +5 */
    0x00070742,                /* 006B  gget L7 <- g7   (_SP_BACKSLASH) */
    0x00080842,                /* 006C  gget L8 <- g8   (_SP_QUOTE) */
    0x00060085, 0x00070008,    /* 006D  add L6 <- L7 + L8 */
    0x0028004F,                /* 006F  jmp +40 */
    0x00070A40,                /* 0070  i8 L7 <- 10 */
    0x0008008F, 0x00050007,    /* 0071  eq L8 <- L5 == L7 */
    0x0005084B,                /* 0073  jf L8, +5 */
    0x00070742,                /* 0074  gget L7 <- g7   (_SP_BACKSLASH) */
    0x00020846,                /* 0075  lconst L8 <- static#2   ("n") */
    0x00060085, 0x00070008,    /* 0076  add L6 <- L7 + L8 */
    0x001F004F,                /* 0078  jmp +31 */
    0x00070D40,                /* 0079  i8 L7 <- 13 */
    0x0008008F, 0x00050007,    /* 007A  eq L8 <- L5 == L7 */
    0x0005084B,                /* 007C  jf L8, +5 */
    0x00070742,                /* 007D  gget L7 <- g7   (_SP_BACKSLASH) */
    0x00030846,                /* 007E  lconst L8 <- static#3   ("r") */
    0x00060085, 0x00070008,    /* 007F  add L6 <- L7 + L8 */
    0x0016004F,                /* 0081  jmp +22 */
    0x00070940,                /* 0082  i8 L7 <- 9 */
    0x0008008F, 0x00050007,    /* 0083  eq L8 <- L5 == L7 */
    0x0005084B,                /* 0085  jf L8, +5 */
    0x00070742,                /* 0086  gget L7 <- g7   (_SP_BACKSLASH) */
    0x00040846,                /* 0087  lconst L8 <- static#4   ("t") */
    0x00060085, 0x00070008,    /* 0088  add L6 <- L7 + L8 */
    0x000D004F,                /* 008A  jmp +13 */
    0x00082040,                /* 008B  i8 L8 <- 32 */
    0x00070091, 0x00050008,    /* 008C  lt L7 <- L5 < L8 */
    0x0003074C,                /* 008E  jt L7, +3 */
    0x00087F40,                /* 008F  i8 L8 <- 127 */
    0x0007008F, 0x00050008,    /* 0090  eq L7 <- L5 == L8 */
    0x0005074B,                /* 0092  jf L7, +5 */
    0x00010742,                /* 0093  gget L7 <- g1   (_sp_hex_escape) */
    0x00080541,                /* 0094  move L8 <- L5 */
    0x00070151,                /* 0095  call base=L7 argc=1 */
    0x00070041,                /* 0096  move L7 <- L0 */
    0x00060741,                /* 0097  move L6 <- L7 */
    0x00050746,                /* 0098  lconst L7 <- static#5   ("nil") */
    0x00080097, 0x00060007,    /* 0099  is L8 <- L6 is L7 */
    0x0004084B,                /* 009B  jf L8, +4 */
    0x00070140,                /* 009C  i8 L7 <- 1 */
    0x00040085, 0x00040007,    /* 009D  add L4 <- L4 + L7 */
    0x0012004F,                /* 009F  jmp +18 */
    0x00070091, 0x00030004,    /* 00A0  lt L7 <- L3 < L4 */
    0x0009074B,                /* 00A2  jf L7, +9 */
    0x00078041,                /* 00A3  move L7 <- P0 */
    0x00080341,                /* 00A4  move L8 <- L3 */
    0x00090086, 0x00040003,    /* 00A5  sub L9 <- L4 - L3 */
    0x000702B7, 0x00020000,    /* 00A7  msg base=L7 argc=2 msg#2   (substr) */
    0x00070041,                /* 00A9  move L7 <- L0 */
    0x00010085, 0x00010007,    /* 00AA  add L1 <- L1 + L7 */
    0x00010085, 0x00010006,    /* 00AC  add L1 <- L1 + L6 */
    0x00070140,                /* 00AE  i8 L7 <- 1 */
    0x00040085, 0x00040007,    /* 00AF  add L4 <- L4 + L7 */
    0x00030441,                /* 00B1  move L3 <- L4 */
    0xFFA5004F,                /* 00B2  jmp -91 */
    0x00070091, 0x00030002,    /* 00B3  lt L7 <- L3 < L2 */
    0x0009074B,                /* 00B5  jf L7, +9 */
    0x00078041,                /* 00B6  move L7 <- P0 */
    0x00080341,                /* 00B7  move L8 <- L3 */
    0x00090086, 0x00020003,    /* 00B8  sub L9 <- L2 - L3 */
    0x000702B7, 0x00020000,    /* 00BA  msg base=L7 argc=2 msg#2   (substr) */
    0x00070041,                /* 00BC  move L7 <- L0 */
    0x00010085, 0x00010007,    /* 00BD  add L1 <- L1 + L7 */
    0x00080742,                /* 00BF  gget L7 <- g8   (_SP_QUOTE) */
    0x00080085, 0x00010007,    /* 00C0  add L8 <- L1 + L7 */
    0x00080008,                /* 00C2  return L8 */
    0x000C0442,                /* 00C3  gget L4 <- g12   (str) */
    0x00058041,                /* 00C4  move L5 <- P0 */
    0x00040151,                /* 00C5  call base=L4 argc=1 */
    0x00040041,                /* 00C6  move L4 <- L0 */
    0x00010441,                /* 00C7  move L1 <- L4 */
    0x000B0442,                /* 00C8  gget L4 <- g11   (len) */
    0x00050141,                /* 00C9  move L5 <- L1 */
    0x00040151,                /* 00CA  call base=L4 argc=1 */
    0x00040041,                /* 00CB  move L4 <- L0 */
    0x00020441,                /* 00CC  move L2 <- L4 */
    0x00030040,                /* 00CD  i8 L3 <- 0 */
    0x00040091, 0x00030002,    /* 00CE  lt L4 <- L3 < L2 */
    0x0027044B,                /* 00D0  jf L4, +39 */
    0x00040098, 0x00010003,    /* 00D1  getidx L4 <- L1[L3] */
    0x00056540,                /* 00D3  i8 L5 <- 101 */
    0x0006008F, 0x00040005,    /* 00D4  eq L6 <- L4 == L5 */
    0x001D064B,                /* 00D6  jf L6, +29 */
    0x00040141,                /* 00D7  move L4 <- L1 */
    0x00050040,                /* 00D8  i8 L5 <- 0 */
    0x00060140,                /* 00D9  i8 L6 <- 1 */
    0x00070085, 0x00030006,    /* 00DA  add L7 <- L3 + L6 */
    0x00060741,                /* 00DC  move L6 <- L7 */
    0x000402B7, 0x00020000,    /* 00DD  msg base=L4 argc=2 msg#2   (substr) */
    0x00040041,                /* 00DF  move L4 <- L0 */
    0x00040542,                /* 00E0  gget L5 <- g4   (_sp_exponent) */
    0x00060141,                /* 00E1  move L6 <- L1 */
    0x00070140,                /* 00E2  i8 L7 <- 1 */
    0x00080085, 0x00030007,    /* 00E3  add L8 <- L3 + L7 */
    0x00070841,                /* 00E5  move L7 <- L8 */
    0x00080086, 0x00020003,    /* 00E6  sub L8 <- L2 - L3 */
    0x00090140,                /* 00E8  i8 L9 <- 1 */
    0x000A0086, 0x00080009,    /* 00E9  sub L10 <- L8 - L9 */
    0x00080A41,                /* 00EB  move L8 <- L10 */
    0x000602B7, 0x00020000,    /* 00EC  msg base=L6 argc=2 msg#2   (substr) */
    0x00060041,                /* 00EE  move L6 <- L0 */
    0x00050151,                /* 00EF  call base=L5 argc=1 */
    0x00050041,                /* 00F0  move L5 <- L0 */
    0x00060085, 0x00040005,    /* 00F1  add L6 <- L4 + L5 */
    0x00060008,                /* 00F3  return L6 */
    0x00040140,                /* 00F4  i8 L4 <- 1 */
    0x00030085, 0x00030004,    /* 00F5  add L3 <- L3 + L4 */
    0xFFD6004F,                /* 00F7  jmp -42 */
    0x00010008,                /* 00F8  return L1 */
    0x00060146,                /* 00F9  lconst L1 <- static#6   ("") */
    0x00020040,                /* 00FA  i8 L2 <- 0 */
    0x00040040,                /* 00FB  i8 L4 <- 0 */
    0x00050098, 0x80000004,    /* 00FC  getidx L5 <- P0[L4] */
    0x00062D40,                /* 00FE  i8 L6 <- 45 */
    0x0007008F, 0x00050006,    /* 00FF  eq L7 <- L5 == L6 */
    0x0003074B,                /* 0101  jf L7, +3 */
    0x00070146,                /* 0102  lconst L1 <- static#7   ("-") */
    0x00020140,                /* 0103  i8 L2 <- 1 */
    0x0008004F,                /* 0104  jmp +8 */
    0x00040040,                /* 0105  i8 L4 <- 0 */
    0x00050098, 0x80000004,    /* 0106  getidx L5 <- P0[L4] */
    0x00062B40,                /* 0108  i8 L6 <- 43 */
    0x0007008F, 0x00050006,    /* 0109  eq L7 <- L5 == L6 */
    0x0001074B,                /* 010B  jf L7, +1 */
    0x00020140,                /* 010C  i8 L2 <- 1 */
    0x000B0442,                /* 010D  gget L4 <- g11   (len) */
    0x00058041,                /* 010E  move L5 <- P0 */
    0x00040151,                /* 010F  call base=L4 argc=1 */
    0x00040041,                /* 0110  move L4 <- L0 */
    0x00030441,                /* 0111  move L3 <- L4 */
    0x00050140,                /* 0112  i8 L5 <- 1 */
    0x00060086, 0x00030005,    /* 0113  sub L6 <- L3 - L5 */
    0x00040091, 0x00020006,    /* 0115  lt L4 <- L2 < L6 */
    0x0005044B,                /* 0117  jf L4, +5 */
    0x00050098, 0x80000002,    /* 0118  getidx L5 <- P0[L2] */
    0x00063040,                /* 011A  i8 L6 <- 48 */
    0x0004008F, 0x00050006,    /* 011B  eq L4 <- L5 == L6 */
    0x0004044B,                /* 011D  jf L4, +4 */
    0x00040140,                /* 011E  i8 L4 <- 1 */
    0x00020085, 0x00020004,    /* 011F  add L2 <- L2 + L4 */
    0xFFF0004F,                /* 0121  jmp -16 */
    0x00048041,                /* 0122  move L4 <- P0 */
    0x00050241,                /* 0123  move L5 <- L2 */
    0x00060086, 0x00030002,    /* 0124  sub L6 <- L3 - L2 */
    0x000402B7, 0x00020000,    /* 0126  msg base=L4 argc=2 msg#2   (substr) */
    0x00040041,                /* 0128  move L4 <- L0 */
    0x00050085, 0x00010004,    /* 0129  add L5 <- L1 + L4 */
    0x00050008,                /* 012B  return L5 */
    0x00060146,                /* 012C  lconst L1 <- static#6   ("") */
    0x00020104,                /* 012D  lbool L2 <- 1 */
    0x000400AE, 0x80000000,    /* 012E  iter L4 <- P0 */
    0x000300AF, 0x0004000D,    /* 0130  itnext L3 <- L4, done +13 */
    0x0005024A,                /* 0132  not L5 <- L2 */
    0x0003054B,                /* 0133  jf L5, +3 */
    0x00080546,                /* 0134  lconst L5 <- static#8   (" ") */
    0x00010085, 0x00010005,    /* 0135  add L1 <- L1 + L5 */
    0x00060542,                /* 0137  gget L5 <- g6   (write) */
    0x00060341,                /* 0138  move L6 <- L3 */
    0x00050151,                /* 0139  call base=L5 argc=1 */
    0x00050041,                /* 013A  move L5 <- L0 */
    0x00010085, 0x00010005,    /* 013B  add L1 <- L1 + L5 */
    0x00020004,                /* 013D  lbool L2 <- 0 */
    0xFFF1004F,                /* 013E  jmp -15 */
    0x00010008,                /* 013F  return L1 */
    0x00050446,                /* 0140  lconst L4 <- static#5   ("nil") */
    0x00050097, 0x80000004,    /* 0141  is L5 <- P0 is L4 */
    0x0002054B,                /* 0143  jf L5, +2 */
    0x00090446,                /* 0144  lconst L4 <- static#9   ("()") */
    0x00040008,                /* 0145  return L4 */
    0x000A0446,                /* 0146  lconst L4 <- static#10   ("pair") */
    0x00050097, 0x80000004,    /* 0147  is L5 <- P0 is L4 */
    0x002E054B,                /* 0149  jf L5, +46 */
    0x000B0146,                /* 014A  lconst L1 <- static#11   ("(") */
    0x00028041,                /* 014B  move L2 <- P0 */
    0x00030104,                /* 014C  lbool L3 <- 1 */
    0x000A0446,                /* 014D  lconst L4 <- static#10   ("pair") */
    0x00050097, 0x00020004,    /* 014E  is L5 <- L2 is L4 */
    0x0015054B,                /* 0150  jf L5, +21 */
    0x0004034A,                /* 0151  not L4 <- L3 */
    0x0003044B,                /* 0152  jf L4, +3 */
    0x00080446,                /* 0153  lconst L4 <- static#8   (" ") */
    0x00010085, 0x00010004,    /* 0154  add L1 <- L1 + L4 */
    0x00060442,                /* 0156  gget L4 <- g6   (write) */
    0x000D0542,                /* 0157  gget L5 <- g13   (car) */
    0x00060241,                /* 0158  move L6 <- L2 */
    0x00050151,                /* 0159  call base=L5 argc=1 */
    0x00050041,                /* 015A  move L5 <- L0 */
    0x00040151,                /* 015B  call base=L4 argc=1 */
    0x00040041,                /* 015C  move L4 <- L0 */
    0x00010085, 0x00010004,    /* 015D  add L1 <- L1 + L4 */
    0x00030004,                /* 015F  lbool L3 <- 0 */
    0x000E0442,                /* 0160  gget L4 <- g14   (cdr) */
    0x00050241,                /* 0161  move L5 <- L2 */
    0x00040151,                /* 0162  call base=L4 argc=1 */
    0x00040041,                /* 0163  move L4 <- L0 */
    0x00020441,                /* 0164  move L2 <- L4 */
    0xFFE7004F,                /* 0165  jmp -25 */
    0x00050446,                /* 0166  lconst L4 <- static#5   ("nil") */
    0x00050097, 0x00020004,    /* 0167  is L5 <- L2 is L4 */
    0x0006054A,                /* 0169  not L6 <- L5 */
    0x0009064B,                /* 016A  jf L6, +9 */
    0x000C0446,                /* 016B  lconst L4 <- static#12   (" . ") */
    0x00050085, 0x00010004,    /* 016C  add L5 <- L1 + L4 */
    0x00060642,                /* 016E  gget L6 <- g6   (write) */
    0x00070241,                /* 016F  move L7 <- L2 */
    0x00060151,                /* 0170  call base=L6 argc=1 */
    0x00060041,                /* 0171  move L6 <- L0 */
    0x00010085, 0x00050006,    /* 0172  add L1 <- L5 + L6 */
    0x000D0446,                /* 0174  lconst L4 <- static#13   (")") */
    0x00050085, 0x00010004,    /* 0175  add L5 <- L1 + L4 */
    0x00050008,                /* 0177  return L5 */
    0x000E0446,                /* 0178  lconst L4 <- static#14   ("sym") */
    0x00050097, 0x80000004,    /* 0179  is L5 <- P0 is L4 */
    0x0005054B,                /* 017B  jf L5, +5 */
    0x000C0442,                /* 017C  gget L4 <- g12   (str) */
    0x00058041,                /* 017D  move L5 <- P0 */
    0x00040151,                /* 017E  call base=L4 argc=1 */
    0x00040041,                /* 017F  move L4 <- L0 */
    0x00040008,                /* 0180  return L4 */
    0x000F0446,                /* 0181  lconst L4 <- static#15   ("str") */
    0x00050097, 0x80000004,    /* 0182  is L5 <- P0 is L4 */
    0x0005054B,                /* 0184  jf L5, +5 */
    0x00020442,                /* 0185  gget L4 <- g2   (quote) */
    0x00058041,                /* 0186  move L5 <- P0 */
    0x00040151,                /* 0187  call base=L4 argc=1 */
    0x00040041,                /* 0188  move L4 <- L0 */
    0x00040008,                /* 0189  return L4 */
    0x00100446,                /* 018A  lconst L4 <- static#16   ("bool") */
    0x00050097, 0x80000004,    /* 018B  is L5 <- P0 is L4 */
    0x0005054B,                /* 018D  jf L5, +5 */
    0x0002804B,                /* 018E  jf P0, +2 */
    0x00110446,                /* 018F  lconst L4 <- static#17   ("#<true>") */
    0x00040008,                /* 0190  return L4 */
    0x00120446,                /* 0191  lconst L4 <- static#18   ("#<false>") */
    0x00040008,                /* 0192  return L4 */
    0x00130446,                /* 0193  lconst L4 <- static#19   ("int") */
    0x00050097, 0x80000004,    /* 0194  is L5 <- P0 is L4 */
    0x0005054B,                /* 0196  jf L5, +5 */
    0x000C0442,                /* 0197  gget L4 <- g12   (str) */
    0x00058041,                /* 0198  move L5 <- P0 */
    0x00040151,                /* 0199  call base=L4 argc=1 */
    0x00040041,                /* 019A  move L4 <- L0 */
    0x00040008,                /* 019B  return L4 */
    0x00140446,                /* 019C  lconst L4 <- static#20   ("float") */
    0x00050097, 0x80000004,    /* 019D  is L5 <- P0 is L4 */
    0x0005054B,                /* 019F  jf L5, +5 */
    0x00030442,                /* 01A0  gget L4 <- g3   (spell_float) */
    0x00058041,                /* 01A1  move L5 <- P0 */
    0x00040151,                /* 01A2  call base=L4 argc=1 */
    0x00040041,                /* 01A3  move L4 <- L0 */
    0x00040008,                /* 01A4  return L4 */
    0x00150446,                /* 01A5  lconst L4 <- static#21   ("list") */
    0x00050097, 0x80000004,    /* 01A6  is L5 <- P0 is L4 */
    0x000B054B,                /* 01A8  jf L5, +11 */
    0x00160446,                /* 01A9  lconst L4 <- static#22   ("#native(") */
    0x00050542,                /* 01AA  gget L5 <- g5   (_sp_items) */
    0x00068041,                /* 01AB  move L6 <- P0 */
    0x00050151,                /* 01AC  call base=L5 argc=1 */
    0x00050041,                /* 01AD  move L5 <- L0 */
    0x00060085, 0x00040005,    /* 01AE  add L6 <- L4 + L5 */
    0x000D0746,                /* 01B0  lconst L7 <- static#13   (")") */
    0x00080085, 0x00060007,    /* 01B1  add L8 <- L6 + L7 */
    0x00080008,                /* 01B3  return L8 */
    0x00170446,                /* 01B4  lconst L4 <- static#23   ("tuple") */
    0x00050097, 0x80000004,    /* 01B5  is L5 <- P0 is L4 */
    0x000B054B,                /* 01B7  jf L5, +11 */
    0x00180446,                /* 01B8  lconst L4 <- static#24   ("#tuple(") */
    0x00050542,                /* 01B9  gget L5 <- g5   (_sp_items) */
    0x00068041,                /* 01BA  move L6 <- P0 */
    0x00050151,                /* 01BB  call base=L5 argc=1 */
    0x00050041,                /* 01BC  move L5 <- L0 */
    0x00060085, 0x00040005,    /* 01BD  add L6 <- L4 + L5 */
    0x000D0746,                /* 01BF  lconst L7 <- static#13   (")") */
    0x00080085, 0x00060007,    /* 01C0  add L8 <- L6 + L7 */
    0x00080008,                /* 01C2  return L8 */
    0x00190446,                /* 01C3  lconst L4 <- static#25   ("#<object>") */
    0x00040008,                /* 01C4  return L4 */
};

const wy_module_image sexp_print_image = {
    .name = "sexp_print",
    .sections = {
        [WY_SEC_HEADER] = { sexp_print_header, sizeof sexp_print_header },
        [WY_SEC_STATICS] = { sexp_print_statics, sizeof sexp_print_statics },
        [WY_SEC_SYMBOLS] = { sexp_print_symbols, sizeof sexp_print_symbols },
        [WY_SEC_FUNCTIONS] = { sexp_print_functions, sizeof sexp_print_functions },
        [WY_SEC_CLASSES] = { sexp_print_classes, sizeof sexp_print_classes },
        [WY_SEC_MESSAGES] = { sexp_print_messages, sizeof sexp_print_messages },
        [WY_SEC_CODE] = { (const uint8_t*) sexp_print_code, sizeof sexp_print_code },
        [WY_SEC_EXPORTS] = { sexp_print_exports, sizeof sexp_print_exports },
        [WY_SEC_FREE] = { sexp_print_free, sizeof sexp_print_free },
    },
};
