#ifndef WYRM_GC_FLAGS_H
#define WYRM_GC_FLAGS_H

#include <wyrm/sys/toolchain.h>

WY_BEGIN_DECLS

enum
{
    WY_GC_STATIC          = 0x001,
    WY_GC_FLAG_MARKED     = 0x004,
    WY_GC_FLAG_FINALIZED  = 0x008,
    WY_GC_FLAG_RO         = 0x010,

    /**
     * A wy_string holding a wyc `binary` static (wyc-format.md §4.2), not
     * text - str/len are raw bytes rather than UTF-8. Placeholder until the
     * bytes type exists (epic 7); record-only, nothing reads it yet.
     */
    WY_GC_FLAG_BINARY     = 0x020,
};

WY_END_DECLS

#endif
