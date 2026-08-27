#ifndef WYRM_VM_H_
#define WYRM_VM_H_

#include <wyrm/wcore.h>

WYRM_BEGIN_DECLS

wyrm_error wy_vm_exec_bytecode(wyrm_context* ctx, size_t pos, const wy_u32 buffer[], size_t len);

WYRM_END_DECLS


#endif
