#include <wyrm/link.h>

#include <wyrm/module.h>
#include <wyrm/slot.h>

wy_error wy_link_fill_from_builtins(wy_context* context, wy_module* module, wy_module* builtins)
{
    WY_UNUSED(context);
    if (module == WY_NULL) { return WY_ERR_INVAL; }
    if (builtins == WY_NULL) { return WY_ERR_NONE; }

    const wy_slot_dict* free_names = &module->free_names;
    for (wy_uword i = 0; i < free_names->capacity; i++) {
        const wy_slot_dict_entry* entry = &free_names->entry_table[i];
        if (entry->symbol == WY_SYMBOL_INVALID) { continue; }

        wy_uword module_slot = entry->slot;
        if (module_slot >= module->global_count) { continue; }
        if (module->fill_layer[module_slot] != 0) { continue; }

        wy_uword builtins_slot = wy_slot_dict_get((wy_slot_dict*) &builtins->exports, entry->symbol);
        if (builtins_slot == WY_SLOT_INVALID) { continue; }
        if (builtins_slot >= builtins->global_count) { continue; }

        module->globals[module_slot] = builtins->globals[builtins_slot];
        module->fill_layer[module_slot] = 3;
        module->fill_source[module_slot] = entry->symbol;
    }

    return WY_ERR_NONE;
}
