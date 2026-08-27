#include <wyrm/slot.h>

// ----------------------------------------------------------------------------
// Slot Dictionary
// ----------------------------------------------------------------------------

wy_error wy_slot_dict_expand_f(wy_slot_dict* self, wy_allocator* allocator, wy_uword new_capacity)
{
    WYRM_ASSERT(new_capacity > self->capacity);
    wy_slot_dict_entry* old_table = self->entry_table;
    wy_slot_dict_entry* new_table = (wy_slot_dict_entry*) wyrm_allocator_alloc(allocator, sizeof(wy_slot_dict_entry) * new_capacity);
    if (new_table == WYRM_NULL)
    {
        return WYRM_ERR_NOMEM;
    }

    for (wy_uword i = 0; i < new_capacity; ++i) {
        new_table[i].symbol = WYRM_SYMBOL_INVALID;
        new_table[i].slot = WY_SLOT_INVALID;
    }

    for (wy_uword i = 0; i < self->capacity; ++i) {
        if (old_table[i].symbol != WYRM_SYMBOL_INVALID) {
            wy_slot_dict_entry* entry = wy_slot_dict_find_entry_(new_table, new_capacity, old_table[i].symbol);
            entry->symbol = old_table[i].symbol;
            entry->slot = old_table[i].slot;
        }
    }
    wyrm_allocator_free(allocator, old_table);
    self->entry_table = new_table;
    self->capacity = new_capacity;
    return WYRM_ERR_NONE;
}


wy_error wy_slot_dict_add_entry(wy_slot_dict* self, wy_symbol sym, wy_uword idx)
{
    wy_slot_dict_entry* entry = wy_slot_dict_find_entry_(self->entry_table, self->capacity, sym);
    if (entry == WYRM_NULL) { return WYRM_ERR_NOMEM; }
    if (entry->symbol != WYRM_SYMBOL_INVALID) { return WYRM_ERR_EXISTS; }
    entry->symbol = sym;
    entry->slot = idx;
    self->entry_count++;
    return WYRM_ERR_NONE;
}


void wy_slot_finalize_f(wy_slot_dict* self, wy_allocator* allocator)
{
    wyrm_allocator_free(allocator, self->entry_table);
    self->entry_table = WYRM_NULL;
    self->capacity = 0;
    self->entry_count = 0;
}
