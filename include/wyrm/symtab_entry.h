#ifndef WYRM_SYMTAB_ENTRY_H
#define WYRM_SYMTAB_ENTRY_H

#include <wyrm/sys/toolchain.h>

WYRM_BEGIN_DECLS

/**
 * @brief Symbol table entry
 */
typedef const char* wyrm_symtab_entry;
typedef wyrm_symtab_entry wy_symbol;
#define WYRM_SYMBOL_INVALID ((wy_symbol)WYRM_NULL)

WYRM_END_DECLS

#endif
