#ifndef WYRM_WPROTOTYPE_H_
#define WYRM_WPROTOTYPE_H_

#include <wyrm/context.h>
#include <wyrm/symtab_entry.h>
#include <wyrm/value.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WY_BAD_SLOT WY_UWORD_MAX
#define WY_SLOT_DEFAULTS 0

enum
{
    WY_PROTOTYPE_SLOT_FLAG_BOXED  = 0x0001,  ///< Slot is boxed, may escape
    WY_PROTOTYPE_SLOT_FLAG_STATIC = 0x0002,  ///< Slot is statically allocated
};

typedef struct wy_prototype_slot
{
    wy_uword flags;
    wy_symtab_entry symtab_entry;
    wy_value default_value;
} wy_prototype_slot;


struct wy_prototype
{
    wy_object object;
    wy_prototype_slot* slots;
    wy_uword slot_capacity;
    wy_uword slot_count;
};


/**
 * @brief Get slot count
 */
WY_INLINE wy_uword wy_prototype_get_slot_count_f(wy_prototype* self)
{
    return self->slot_count;
}

/**
 * @brief Get slot capacity
 */
WY_INLINE wy_uword wy_prototype_get_slot_capacity_f(wy_prototype* self)
{
    return self->slot_capacity;
}

/**
 * @brief Initialize prototype
 */
WY_INLINE void wy_prototype_init_f(wy_prototype* self)
{
    self->slot_count = 0;
    self->slot_capacity = 0;
    self->slots = WY_NULL;
}

/**
 * @brief Finalize
 */
WY_INLINE void wy_prototype_finalize_f(wy_context* context, wy_prototype* self)
{
    wy_context_gc_free(context, self->slots);
    self->slots = WY_NULL;
    self->slot_capacity = 0;
    self->slot_count = 0;
}

/**
 * @brief Resize slot capacity
 */
WY_INLINE wy_error wy_prototype_reserve_f(wy_context* context, wy_prototype* self, wy_uword size)
{
    if (size < wy_prototype_get_slot_count_f(self)) { return WY_ERR_RANGE; }
    wy_prototype_slot* slot_array = (wy_prototype_slot*) wy_context_gc_realloc(context, self->slots, sizeof(wy_prototype_slot) * size);
    if (slot_array == WY_NULL) { return WY_ERR_NOMEM; }
    self->slots = slot_array;
    self->slot_capacity = size;
    return WY_ERR_NONE;
}

/**
 * @brief Add slot to the class (construction helper)
 */
WY_INLINE wy_error wy_prototype_add_slot_f(wy_prototype* self, wy_symtab_entry slot_name, wy_uword flags)
{
    if (wy_prototype_get_slot_count_f(self) >= wy_prototype_get_slot_capacity_f(self)) { return WY_ERR_NOMEM; }
    self->slots[self->slot_count].symtab_entry = slot_name;
    self->slots[self->slot_count].flags = flags;
    self->slot_count++;
    return WY_ERR_NONE;
}

/**
 * @brief Get a selector for the given symbol
 */
WY_INLINE wy_uword wy_prototype_get_slot_selector_f(wy_prototype* self, wy_symtab_entry entry)
{
    for (wy_uword i = 0; i < wy_prototype_get_slot_count_f(self); i++) {
        if (self->slots[i].symtab_entry == entry) {
            return i;
        }
    }
    return WY_BAD_SLOT;
}

#ifdef __cplusplus
}
#endif

#endif
