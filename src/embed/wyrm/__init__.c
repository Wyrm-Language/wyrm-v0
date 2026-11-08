/* Generated from __init__ by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

_Static_assert(WY_IMAGE_VERSION == 342561947u, "compiled module is for another .wyc format version; recompile it");

static const uint8_t __init___header[] = {
    0xA2, 0x61, 0x6E, 0x68, 0x5F, 0x5F, 0x69, 0x6E, 0x69, 0x74, 0x5F, 0x5F,
    0x61, 0x67, 0x00,
};

static const uint8_t __init___statics[] = {
    0x80,
};

static const uint8_t __init___symbols[] = {
    0x80,
};

static const uint8_t __init___functions[] = {
    0x81, 0xA5, 0x61, 0x6E, 0x66, 0x3C, 0x69, 0x6E, 0x69, 0x74, 0x3E, 0x61,
    0x70, 0x80, 0x61, 0x6C, 0x01, 0x61, 0x63, 0x00, 0x61, 0x66, 0x00,
};

static const uint8_t __init___classes[] = {
    0x80,
};

static const uint8_t __init___messages[] = {
    0x80,
};

static const uint32_t __init___code[] = {
    0x00000003,                /* 0000  lnil L0 */
    0x00000008,                /* 0001  return L0 */
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
