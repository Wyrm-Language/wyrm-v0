/* Generated from __init__ by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

static const uint8_t __init___header[] = {
    0xA3, 0x61, 0x6E, 0x68, 0x5F, 0x5F, 0x69, 0x6E, 0x69, 0x74, 0x5F, 0x5F,
    0x61, 0x67, 0x00, 0x61, 0x6C, 0x00,
};

static const uint8_t __init___statics[] = {
    0x80,
};

static const uint8_t __init___symbols[] = {
    0x80,
};

static const uint8_t __init___functions[] = {
    0x80,
};

static const uint8_t __init___classes[] = {
    0x80,
};

static const uint8_t __init___messages[] = {
    0x80,
};

static const uint32_t __init___code[] = {
    0x00000002,                /* 0000  return count=0 */
};

const wy_module_image __init___image = {
    .name = "__init__",
    .sections = {
        [WY_SEC_HEADER] = { __init___header, sizeof __init___header },
        [WY_SEC_STATICS] = { __init___statics, sizeof __init___statics },
        [WY_SEC_SYMBOLS] = { __init___symbols, sizeof __init___symbols },
        [WY_SEC_FUNCTIONS] = { __init___functions, sizeof __init___functions },
        [WY_SEC_CLASSES] = { __init___classes, sizeof __init___classes },
        [WY_SEC_MESSAGES] = { __init___messages, sizeof __init___messages },
        [WY_SEC_CODE] = { (const uint8_t*) __init___code, sizeof __init___code },
    },
};
