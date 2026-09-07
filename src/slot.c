#include <wyrm/slot.h>

// ----------------------------------------------------------------------------
// Slot Dictionary
// ----------------------------------------------------------------------------

wy_error wy_slot_dict_expand_f(wy_slot_dict* self, wy_allocator* allocator, wy_uword new_capacity)
{
    WY_ASSERT(new_capacity > self->capacity);
    wy_slot_dict_entry* old_table = self->entry_table;
    wy_slot_dict_entry* new_table = (wy_slot_dict_entry*) wy_allocator_alloc(allocator, sizeof(wy_slot_dict_entry) * new_capacity);
    if (new_table == WY_NULL)
    {
        return WY_ERR_NOMEM;
    }

    for (wy_uword i = 0; i < new_capacity; ++i) {
        new_table[i].symbol = WY_SYMBOL_INVALID;
        new_table[i].slot = WY_SLOT_INVALID;
    }

    for (wy_uword i = 0; i < self->capacity; ++i) {
        if (old_table[i].symbol != WY_SYMBOL_INVALID) {
            wy_slot_dict_entry* entry = wy_slot_dict_find_entry_(new_table, new_capacity, old_table[i].symbol);
            entry->symbol = old_table[i].symbol;
            entry->slot = old_table[i].slot;
        }
    }
    wy_allocator_free(allocator, old_table);
    self->entry_table = new_table;
    self->capacity = new_capacity;
    return WY_ERR_NONE;
}


wy_error wy_slot_dict_add_entry(wy_slot_dict* self, wy_symbol sym, wy_uword idx)
{
    wy_slot_dict_entry* entry = wy_slot_dict_find_entry_(self->entry_table, self->capacity, sym);
    if (entry == WY_NULL) { return WY_ERR_NOMEM; }
    if (entry->symbol != WY_SYMBOL_INVALID) { return WY_ERR_EXISTS; }
    entry->symbol = sym;
    entry->slot = idx;
    self->entry_count++;
    return WY_ERR_NONE;
}


void wy_slot_finalize_f(wy_slot_dict* self, wy_allocator* allocator)
{
    wy_allocator_free(allocator, self->entry_table);
    self->entry_table = WY_NULL;
    self->capacity = 0;
    self->entry_count = 0;
}
