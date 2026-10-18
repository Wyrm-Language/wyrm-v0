/* Generated from template by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

static const uint8_t template_header[] = {
    0xA3, 0x61, 0x6E, 0x68, 0x74, 0x65, 0x6D, 0x70, 0x6C, 0x61, 0x74, 0x65,
    0x61, 0x67, 0x02, 0x61, 0x6C, 0x04,
};

static const uint8_t template_statics[] = {
    0x80,
};

static const uint8_t template_symbols[] = {
    0x83, 0x68, 0x61, 0x6E, 0x6E, 0x6F, 0x74, 0x61, 0x74, 0x65, 0x68, 0x74,
    0x65, 0x6D, 0x70, 0x6C, 0x61, 0x74, 0x65, 0x64, 0x74, 0x72, 0x75, 0x65,
};

static const uint8_t template_functions[] = {
    0x81, 0xA6, 0x61, 0x6E, 0x69, 0x74, 0x65, 0x6D, 0x70, 0x6C, 0x61, 0x74,
    0x65, 0x21, 0x61, 0x70, 0x80, 0x61, 0x6C, 0x06, 0x61, 0x63, 0x09, 0x61,
    0x66, 0x02, 0x61, 0x74, 0x81, 0x00,
};

static const uint8_t template_classes[] = {
    0x80,
};

static const uint8_t template_messages[] = {
    0x81, 0xA1, 0x61, 0x70, 0x81, 0x01,
};

static const uint8_t template_free[] = {
    0xA2, 0x68, 0x54, 0x72, 0x65, 0x65, 0x42, 0x61, 0x73, 0x65, 0x00, 0x65,
    0x73, 0x65, 0x78, 0x70, 0x72, 0x01,
};

static const uint32_t template_code[] = {
    0x000100A8, 0x00000000,    /* 0000  closure L1 <- fn#0, 0 caps */
    0x00000242,                /* 0002  gget L2 <- g0   (TreeBase) */
    0x00030181, 0x00020000,    /* 0003  tuple L3 <- L2, 1 items */
    0x000000AC, 0x00010003,    /* 0005  reg_msg msg#0 <- L1, types L3   (template) */
    0x00000003,                /* 0007  lnil L0 */
    0x00000008,                /* 0008  return L0 */
    0x00000145,                /* 0009  lsym L1 <- sym#0   ("annotate") */
    0x00010245,                /* 000A  lsym L2 <- sym#1   ("template") */
    0x00020345,                /* 000B  lsym L3 <- sym#2   ("true") */
    0x00040184, 0x00030000,    /* 000C  plist L4 <- L3, 1 items */
    0x00030441,                /* 000E  move L3 <- L4 */
    0x00010442,                /* 000F  gget L4 <- g1   (sexpr) */
    0x00058041,                /* 0010  move L5 <- P0 */
    0x00040151,                /* 0011  call base=L4 argc=1 */
    0x00040041,                /* 0012  move L4 <- L0 */
    0x00050484, 0x00010000,    /* 0013  plist L5 <- L1, 4 items */
    0x00050008,                /* 0015  return L5 */
};

const wy_module_image template_image = {
    .name = "template",
    .sections = {
        [WY_SEC_HEADER] = { template_header, sizeof template_header },
        [WY_SEC_STATICS] = { template_statics, sizeof template_statics },
        [WY_SEC_SYMBOLS] = { template_symbols, sizeof template_symbols },
        [WY_SEC_FUNCTIONS] = { template_functions, sizeof template_functions },
        [WY_SEC_CLASSES] = { template_classes, sizeof template_classes },
        [WY_SEC_MESSAGES] = { template_messages, sizeof template_messages },
        [WY_SEC_CODE] = { (const uint8_t*) template_code, sizeof template_code },
        [WY_SEC_FREE] = { template_free, sizeof template_free },
    },
};
