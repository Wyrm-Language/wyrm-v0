#ifndef WYRM_SYMTAB_H_
#define WYRM_SYMTAB_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>
#include <wyrm/allocator.h>
#include <wyrm/symtab_entry.h>

WY_BEGIN_DECLS

/**
 * @brief Symbol table interning node.
 *
 * Identity of an interned symbol is determined by the "significant prefix":
 * the first 31 UTF-8 codepoints of `text` (or the whole string, whichever is
 * shorter). `sig_len` is the byte length of that prefix; `hash` is the
 * FNV-1a hash over those `sig_len` bytes. `len` is the full byte length of
 * the stored text (not including the trailing NUL). `text` is always
 * NUL-terminated so a wy_symbol can be used directly as a C string.
 */
typedef struct wy_symtab_node
{
    wy_u32 hash;
    wy_u16 sig_len;
    wy_u16 len;
    char text[];
} wy_symtab_node;

/**
 * @brief Symbol interning table, owned by wy_machine.
 */
typedef struct wy_symtab
{
    wy_allocator* allocator;
    wy_symtab_node** buckets;
    wy_uword capacity;
    wy_uword count;
} wy_symtab;

wy_error wy_symtab_init_f(wy_symtab* self, wy_allocator* allocator);
void wy_symtab_finalize_f(wy_symtab* self);

/**
 * Intern a UTF-8 string into the symbol table.
 *
 * @param self Symbol table
 * @param utf8 Bytes of the string to intern (need not be NUL-terminated)
 * @param len  Byte length of `utf8`
 * @return The interned symbol (a stable pointer into the table's storage,
 *   usable as a NUL-terminated C string), or WY_SYMBOL_INVALID on
 *   allocation failure.
 */
wy_symbol wy_symtab_intern(wy_symtab* self, const char* utf8, wy_uword len);

/**
 * Look up a UTF-8 string in the symbol table without interning it.
 *
 * @return The existing interned symbol, or WY_SYMBOL_INVALID if not present.
 */
wy_symbol wy_symtab_lookup(wy_symtab* self, const char* utf8, wy_uword len);

/**
 * Compute the byte length of the "significant prefix" (first 31 UTF-8
 * codepoints, or fewer if the string is shorter) of a buffer.
 */
wy_uword wy_symtab_significant_prefix_len(const char* utf8, wy_uword len);

WY_END_DECLS

#endif
