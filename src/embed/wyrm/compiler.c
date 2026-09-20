/* Generated from compiler by wypoc's bytecode compiler. */

#include <stdint.h>
#include "wyrm/image.h"

static const uint8_t compiler_header[] = {
    0x2A, 0x00, 0x00, 0x00, 0x02, 0x6E, 0x00, 0x09, 0x00, 0x00, 0x00, 0x63,
    0x6F, 0x6D, 0x70, 0x69, 0x6C, 0x65, 0x72, 0x00, 0x10, 0x76, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x10, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x6C,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_statics[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_symbols[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_functions[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_classes[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_messages[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_exports[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t compiler_free[] = {
    0x05, 0x00, 0x00, 0x00, 0x00,
};

static const uint32_t compiler_code[] = {
    0x00000002,                /* 0000  return count=0 */
};

const wy_module_image compiler_image = {
    .name = "compiler",
    .sections = {
        [WY_SEC_HEADER] = { compiler_header, sizeof compiler_header },
        [WY_SEC_STATICS] = { compiler_statics, sizeof compiler_statics },
        [WY_SEC_SYMBOLS] = { compiler_symbols, sizeof compiler_symbols },
        [WY_SEC_FUNCTIONS] = { compiler_functions, sizeof compiler_functions },
        [WY_SEC_CLASSES] = { compiler_classes, sizeof compiler_classes },
        [WY_SEC_MESSAGES] = { compiler_messages, sizeof compiler_messages },
        [WY_SEC_CODE] = { (const uint8_t*) compiler_code, sizeof compiler_code },
        [WY_SEC_EXPORTS] = { compiler_exports, sizeof compiler_exports },
        [WY_SEC_FREE] = { compiler_free, sizeof compiler_free },
    },
};
