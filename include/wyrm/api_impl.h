#ifndef WYRM_API_IMPL_H_
#define WYRM_API_IMPL_H_

#include <wyrm/types.h>
#include <wyrm/internal_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WYRM_OBJECT_LIST_INITIAL_SZ
#define WYRM_OBJECT_LIST_INITIAL_SZ 4
#endif

/**
 * Initialize object list with given allocator and object list
 *
 * allocator must be WYRM_NULL or the object array passed into the object list
 * must be allocated from the allocator.
 */
WYRM_INLINE void wyrm_object_list_init_s(wyrm_object_list* object_list, wyrm_allocator* allocator, wyrm_object** obj_list, wyrm_uword capacity)
{
    WYRM_ASSERT(object_list != WYRM_NULL);
    WYRM_ASSERT(obj_list != WYRM_NULL || capacity == 0);
    object_list->allocator = allocator;
    object_list->count = 0;
    object_list->objects = obj_list;
    object_list->capacity = capacity;
}

/**
 * Initialize object list with given allocator and object list
 */
WYRM_INLINE void wyrm_object_list_init_f(wyrm_object_list* object_list, wyrm_allocator* allocator)
{
    WYRM_ASSERT(object_list != WYRM_NULL && allocator != WYRM_NULL);
    wyrm_object_list_init_s(object_list, allocator, WYRM_NULL, 0);
}

/**
 * Get index of list given by idx
 */
WYRM_INLINE wyrm_object* wyrm_object_list_idx_f(wyrm_object_list* object_list, wyrm_uword idx)
{
    WYRM_ASSERT(object_list != WYRM_NULL && idx < object_list->count);
    return object_list->objects[idx];
}

/**
 * Push item onto object list
 */
WYRM_INLINE wyrm_error wyrm_object_list_push(wyrm_object_list* object_list, wyrm_object* obj)
{
    if (object_list == WYRM_NULL) { return WYRM_ERR_INVAL; }
    wyrm_uword new_count = object_list->count + 1;

    if (new_count > object_list->capacity) {
        wyrm_uword update_capacity = wyrm_next_array_capacity(object_list->capacity, WYRM_OBJECT_LIST_INITIAL_SZ);
        wyrm_object** resized = (wyrm_object**) wyrm_allocator_realloc(
            object_list->allocator,
            object_list->objects,
            update_capacity * sizeof(wyrm_object*));
        if (resized == WYRM_NULL) { return WYRM_ERR_NOMEM; }
        object_list->objects = resized;
        object_list->capacity = update_capacity;
    }

    object_list->objects[object_list->count] = obj;
    object_list->count = new_count;
    return WYRM_ERR_NONE;
}

#ifdef __cplusplus
}
#endif

#endif
