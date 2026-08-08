#ifndef WYRM_INL_PROTOTYPE_H_
#define WYRM_INL_PROTOTYPE_H_

#include <wyrm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get slot count
 */
WYRM_INLINE wyrm_uword wyrm_prototype_get_slot_count_f(wyrm_prototype* self)
{
    return self->slot_count;
}

/**
 * @brief Get slot capacity
 */
WYRM_INLINE wyrm_uword wyrm_prototype_get_slot_capacity_f(wyrm_prototype* self)
{
    return self->slot_capacity;
}

/**
 * @brief Initialize prototype
 */
WYRM_INLINE void wyrm_prototype_init_f(wyrm_prototype* self)
{
    self->slot_count = 0;
    self->slot_capacity = 0;
    self->slots = WYRM_NULL;
}

/**
 * @brief Finalize
 */
WYRM_INLINE void wyrm_prototype_finalize_f(wyrm_context* context, wyrm_prototype* self)
{
    wyrm_context_gc_free(context, self->slots);
    self->slots = WYRM_NULL;
    self->slot_capacity = 0;
    self->slot_count = 0;
}

/**
 * @brief Resize slot capacity
 */
WYRM_INLINE wyrm_error wyrm_prototype_reserve_f(wyrm_context* context, wyrm_prototype* self, wyrm_uword size)
{
    if (size < wyrm_prototype_get_slot_count_f(self)) { return WYRM_ERR_RANGE; }
    wyrm_prototype_slot* slot_array = (wyrm_prototype_slot*) wyrm_context_gc_realloc(context, self->slots, sizeof(wyrm_prototype_slot) * size);
    if (slot_array == WYRM_NULL) { return WYRM_ERR_NOMEM; }
    self->slots = slot_array;
    self->slot_capacity = size;
    return WYRM_ERR_NONE;
}

/**
 * @brief Add slot to the class (construction helper)
 */
WYRM_INLINE wyrm_error wyrm_prototype_add_slot_f(wyrm_prototype* self, wyrm_symtab_entry slot_name, wyrm_uword flags)
{
    if (wyrm_prototype_get_slot_count_f(self) >= wyrm_prototype_get_slot_capacity_f(self)) { return WYRM_ERR_NOMEM; }
    self->slots[self->slot_count].symtab_entry = slot_name;
    self->slots[self->slot_count].flags = flags;
    self->slot_count++;
    return WYRM_ERR_NONE;
}

/**
 * @brief Get a selector for the given symbol
 */
WYRM_INLINE wyrm_uword wyrm_prototype_get_slot_selector_f(wyrm_prototype* self, wyrm_symtab_entry entry)
{
    for (wyrm_uword i = 0; i < wyrm_prototype_get_slot_count_f(self); i++) {
        if (self->slots[i].symtab_entry == entry) {
            return i;
        }
    }
    return WYRM_BAD_SLOT;
}

#ifdef __cplusplus
}
#endif

#endif
