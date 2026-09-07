#ifndef WYRM_SYMTAB_ENTRY_H
#define WYRM_SYMTAB_ENTRY_H

#include <wyrm/sys/toolchain.h>

WY_BEGIN_DECLS

/**
 * @brief Symbol table entry
 */
typedef const char* wy_symtab_entry;
typedef wy_symtab_entry wy_symbol;
#define WY_SYMBOL_INVALID ((wy_symbol)WY_NULL)

WY_END_DECLS

#endif
