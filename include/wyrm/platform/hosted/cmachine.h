#ifndef WYRM_PLATFORM_HOSTED_CMACHINE_H
#define WYRM_PLATFORM_HOSTED_CMACHINE_H

#include <wyrm/machine.h>

WY_BEGIN_DECLS

wy_machine* wy_cmachine_new(void);
void wy_cmachine_destroy(wy_machine* machine);

wy_context* wy_cmachine_context_new(wy_machine* machine);
void wy_cmachine_context_destroy(wy_context* ctx);

WY_END_DECLS

#endif
