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
};

WY_END_DECLS

#endif
