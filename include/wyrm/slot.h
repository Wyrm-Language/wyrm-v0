#ifndef WYRM_SLOT_DICT_H_
#define WYRM_SLOT_DICT_H_

#include <wyrm/core.h>
#include <wyrm/allocator.h>
#include <wyrm/util.h>


WYRM_BEGIN_DECLS

typedef struct wy_slot_dict_entry
{
    wy_symbol symbol;
    wy_uword slot;
} wy_slot_dict_entry;

/**
 * Slot Dictionary
 *
 * This is an unordered dictionary that maps a symbol to a slot index. Slot
 * dictionaries are expected to be static, created once on seeing a set of
 * name definitions.
 */
typedef struct wy_slot_dict
{
    wy_slot_dict_entry *entry_table;
    wy_uword entry_count;
    wy_uword capacity;
} wy_slot_dict;

#define WY_SLOT_DICT_INITIALIZER  { .entry_table = WYRM_NULL, .entry_count = 0, .capacity = 0 }

wy_error wy_slot_dict_expand_f(wy_slot_dict* self, wy_allocator* allocator, wy_uword new_capacity);
wy_error wy_slot_dict_add_entry(wy_slot_dict* self, wy_symbol sym, wy_uword idx);
void wy_slot_finalize_f(wy_slot_dict* self, wy_allocator* allocator);

/**
 * Hash lookup for the given symbol
 *
 * @param table Table entries
 * @param capacity Capacity of the table (length)
 * @param symbol Symbol to look or add
 * @return WYRM_NULL if not insertable, insertion point if not found, entry if found
 */
WYRM_INLINE wy_slot_dict_entry* wy_slot_dict_find_entry_(wy_slot_dict_entry table[], wy_uword capacity, wy_symbol symbol)
{
    wy_uword hash = wy_util_rehash((wyrm_uintptr) symbol);
    for (wy_uword i = 0; i < capacity; ++i) {
        wy_slot_dict_entry* entry = &table[(hash + i) % capacity];
        if (entry->symbol == symbol || entry->symbol == WYRM_SYMBOL_INVALID) {
            return entry;
        }
    }
    return WYRM_NULL;
}

/**
 * Lookup a symbol name in the slot dictionary.
 *
 * @param self Slot dictionary
 * @param symbol Symbol name in slot dictionary
 * @return Slot index for the symbol
 */
WYRM_INLINE wyrm_uword wy_slot_dict_get(wy_slot_dict* self, wy_symbol symbol)
{
    wy_slot_dict_entry* entry = wy_slot_dict_find_entry_(self->entry_table, self->capacity, symbol);
    if (entry == WYRM_NULL || entry->symbol != symbol) { return WY_SLOT_INVALID; }
    return entry->slot;
}

WYRM_END_DECLS

#endif
