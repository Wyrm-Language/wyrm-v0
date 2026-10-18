/* wyrm module image descriptors - the declarations a generated .c container
 * needs (doc/llm-bytecode.md 5.2). Originally adopted from pypoc; now
 * maintained here. */
#ifndef WYRM_IMAGE_H
#define WYRM_IMAGE_H

#include <stdint.h>

/* The `.wyc` format version: whole seconds from 2016-01-01T00:00:00Z to the
 * moment the last change to the format was published. Any change to the
 * container, a section's shape, a key or a value encoding sets a new one,
 * here and in WYC_VERSION in src/embed/wyrm/image.wy. A loader accepts
 * exactly this version (doc-llm/llm-bytecode.md section 4). */
#define WY_IMAGE_VERSION 340739680u

typedef struct wy_section_ref {
    const uint8_t *data;
    uint32_t len;
} wy_section_ref;

enum {
    WY_SEC_HEADER = 1,
    WY_SEC_STATICS,
    WY_SEC_SLOT_DEFAULTS,
    WY_SEC_SYMBOLS,
    WY_SEC_FUNCTIONS,
    WY_SEC_CLASSES,
    WY_SEC_MESSAGES,
    WY_SEC_CODE,
    WY_SEC_DEBUG,
    WY_SEC_EXPORTS,
    WY_SEC_FREE,
    WY_SEC_COUNT
};

typedef struct wy_module_image {
    const char *name;
    wy_section_ref sections[WY_SEC_COUNT];
} wy_module_image;

#endif /* WYRM_IMAGE_H */
