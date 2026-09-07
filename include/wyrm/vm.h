#ifndef WYRM_VM_H_
#define WYRM_VM_H_

#include <wyrm/core.h>

WY_BEGIN_DECLS

wy_error wy_vm_exec_bytecode(wy_context* ctx, size_t pos, const wy_u32 buffer[], size_t len);

WY_END_DECLS


#endif
