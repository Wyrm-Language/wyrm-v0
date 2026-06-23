#ifndef WYRM_INTERNAL_API_H_
#define WYRM_INTERNAL_API_H_

#include <wyrm/types.h>
#include <wyrm/sys/string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WYRM_FIXME_STACK_BASE_PTR_TYPE WYRM_NULL  // TODO: define a real frame-header type
#define WYRM_FIXME_STACK_CONT_TYPE     WYRM_NULL  // TODO: define a real continuation-slot type

// ----------------------------------------------------------------------------
// Allocator
// ----------------------------------------------------------------------------

/**
 * Allocates memory of the specified length using the provided allocator.
 *
 * @param self Pointer to the allocator instance used for memory allocation.
 * @param len  The size of memory (in bytes) to allocate.
 * @return A pointer to the allocated memory on success. Returns `WYRM_NULL` if
 *         allocation fails or if the provided allocator is invalid.
 */
static inline void* wyrm_allocator_alloc(wyrm_allocator* self, wyrm_uword len) {
    if (self == WYRM_NULL || self->clz == WYRM_NULL) { return WYRM_NULL; }
    return self->clz->alloc(self, len);
}

/**
 * Allocates a memory region sufficient to hold a header with trailing element array
 *
 * @param self       Pointer to the allocator instance used for memory allocation.
 * @param header_sz  The size (in bytes) of the header to be included in the allocation.
 * @param element_sz The size (in bytes) of a single array element.
 * @param count      The number of elements to allocate in the array.
 * @return A pointer to the allocated memory on success. Returns `WYRM_NULL` if
 *         allocation fails or if the provided allocator is invalid.
 */
static inline void* wyrm_allocator_alloc_array(wyrm_allocator* self, size_t header_sz, wyrm_uword element_sz, wyrm_uword count)
{
    if (self == WYRM_NULL) { return WYRM_NULL; }
    return wyrm_allocator_alloc(self, header_sz + (element_sz * count));
}

/**
 * Reallocates memory for a specified buffer to a new size using the provided allocator.
 *
 * @param self   Pointer to the allocator instance to use for reallocation.
 * @param buffer Pointer to the existing memory buffer to be resized. Can be `NULL` to allocate a new buffer.
 * @param len    The new size of the memory buffer (in bytes).
 * @return A pointer to the reallocated memory buffer on success. Returns `NULL` if the
 *         reallocation fails or if the provided allocator is invalid.
 */
static inline void* wyrm_allocator_realloc(wyrm_allocator* self, void* buffer, wyrm_uword len) {
    if (self == WYRM_NULL || self->clz == WYRM_NULL) { return WYRM_NULL; }
    return self->clz->realloc(self, buffer, len);
}

/**
 * Frees previously allocated memory using the provided allocator.
 *
 * @param self   Pointer to the allocator instance used for memory deallocation.
 * @param buffer Pointer to the memory to be freed. If `NULL`, no action is taken.
 */
static inline void wyrm_allocator_free(wyrm_allocator* self, void* buffer) {
    if (self != WYRM_NULL && self->clz != WYRM_NULL) {
        self->clz->free(self, buffer);
    }
}

// ----------------------------------------------------------------------------
// Stack API
// ----------------------------------------------------------------------------

/**
 * Initialize stack
 *
 * @param self Point to the stack area to initialize
 * @param memory Allocated array of wyrm_value
 * @param capacity Size of allocated array
 */
static inline void wyrm_stack_init_f(wyrm_stack* self, wyrm_value* memory, wyrm_uword capacity)
{
    WYRM_ASSERT(self != WYRM_NULL && memory != WYRM_NULL && capacity > 0);
    self->entries_begin = memory;
    self->entries_end   = memory + capacity;
    self->base          = memory;
    self->top           = memory;
}


/**
 * Get available capacity of stack
 *
 * @param self Pointer to the stack
 */
static inline wyrm_uword wyrm_stack_capacity_remaining(wyrm_stack* self)
{
    return self->entries_end - self->top;
}


/**
 * Push value to the stack
 *
 * @param self Stack object
 * @param type_ref Type of primitive being pushed
 * @param value Primitive value
 * @return WYRM_ERR_NONE on success, WYRM_ERR_INVAL if self is NULL, WYRM_ERR_NOMEM if stack is full
 */
static inline wyrm_error wyrm_stack_push(wyrm_stack* self, wyrm_type_ref type_ref, wyrm_primitive value)
{
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->top == self->entries_end) { return WYRM_ERR_NOMEM; }
    self->top->type = type_ref;
    self->top->data = value;
    self->top++;
    return WYRM_ERR_NONE;
}


/**
 * Pop value from the stack
 *
 * @param self Stack to pop value from
 * @param type_ref Out variable for type
 * @param value Out variable for value
 * @return WYRM_ERR_NONE on success
 */
static inline wyrm_error wyrm_stack_pop(wyrm_stack* self, wyrm_type_ref* type_ref, wyrm_primitive* value)
{
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->top == self->entries_begin) { return WYRM_ERR_EMPTY; }
    self->top--;
    if (type_ref != WYRM_NULL) { *type_ref = self->top->type; }
    if (value != WYRM_NULL)    { *value    = self->top->data; }
    return WYRM_ERR_NONE;
}

/**
 * Start a new stack frame.
 *
 * Pushes two header slots below the new base:
 *   base[-2]  saved base pointer (restored by wyrm_stack_pop_frame)
 *   base[-1]  continuation function (returned by wyrm_stack_pop_frame)
 * Then pushes arg_count argument values above base.
 *
 * @param self         Stack
 * @param args         Array of arguments (may be NULL if arg_count == 0)
 * @param arg_count    Number of arguments
 * @param continuation Function to invoke when this frame's result is ready (may be NULL)
 * @return WYRM_ERR_NONE on success, WYRM_ERR_INVAL if self is NULL, WYRM_ERR_NOMEM if full
 */
static inline wyrm_error wyrm_stack_start_frame(wyrm_stack* self, wyrm_value* args, wyrm_uword arg_count, wyrm_exec_fn continuation)
{
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if ((arg_count + 2) > wyrm_stack_capacity_remaining(self)) { return WYRM_ERR_NOMEM; }

    // base[-2]: saved base pointer
    self->top->type = WYRM_FIXME_STACK_BASE_PTR_TYPE;
    self->top->data.value_ptr = self->base;
    self->top++;

    // base[-1]: continuation
    self->top->type = WYRM_FIXME_STACK_CONT_TYPE;
    self->top->data.cb = continuation;
    self->top++;

    self->base = self->top;

    for (wyrm_uword i = 0; i < arg_count; i++) {
        *self->top = args[i];
        self->top++;
    }
    WYRM_ASSERT(self->top <= self->entries_end);

    return WYRM_ERR_NONE;
}


/**
 * Pop a stack frame, preserving the top N values as results.
 *
 * Restores the base pointer saved by wyrm_stack_start_frame, then moves
 * preserve_count values from the top of the current frame to where the
 * saved-base-pointer slot was, collapsing the frame.
 *
 * @param self           Stack
 * @param preserve_count Number of top-of-frame values to keep as results
 * @return WYRM_ERR_NONE on success
 *         WYRM_ERR_INVAL if self is NULL or no outer frame exists
 *         WYRM_ERR_RANGE if current frame holds fewer than preserve_count values
 */
static inline wyrm_error wyrm_stack_pop_frame(wyrm_stack* self, wyrm_exec_fn* out_continuation, wyrm_uword preserve_count)
{
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->base <= self->entries_begin) { return WYRM_ERR_INVAL; }
    if ((wyrm_uword)(self->top - self->base) < preserve_count) { return WYRM_ERR_RANGE; }

    // Verify expected type (stack corruption otherwise)
    wyrm_value* frame_slot = self->base - 2;
    if (frame_slot[0].type != WYRM_FIXME_STACK_BASE_PTR_TYPE) { return WYRM_ERR_INVAL; }
    if (frame_slot[1].type != WYRM_FIXME_STACK_CONT_TYPE) { return WYRM_ERR_INVAL; }

    // Grab old base (verified ok), move pointers up, return
    wyrm_value* old_base   = frame_slot->data.value_ptr;
    if (out_continuation != WYRM_NULL) { *out_continuation = frame_slot[1].data.cb; }
    wyrm_memmove(frame_slot, self->top - preserve_count, preserve_count * sizeof(wyrm_value));
    self->top  = frame_slot + preserve_count;
    self->base = old_base;
    return WYRM_ERR_NONE;
}


// ----------------------------------------------------------------------------
// Main Loop
// ----------------------------------------------------------------------------

/**
 * Add fd watcher to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_fd(wyrm_main_loop_ref ref, wyrm_primitive *out, wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud)
{
    if (ref == WYRM_NULL || ref->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return ref->vt->add_fd(ref, out, fd, events, priority, cb, ud);
}

/**
 * Add timer to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_timer(wyrm_main_loop_ref self, wyrm_primitive *out, uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_timer(self, out, ms, priority, cb, ud);
}

/**
 * Add idle handler to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_idle(wyrm_main_loop_ref self, wyrm_primitive *out, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_idle(self, out, cb, ud);
}

/**
 * Add wakeable handler to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_wakeable(wyrm_main_loop_ref self, wyrm_primitive *out, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_wakeable(self, out, priority, cb, ud);
}

/**
 * Add triggerable to the main loop
 */
static inline wyrm_error wyrm_main_loop_trigger(wyrm_main_loop_ref self, wyrm_primitive src)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->trigger(self, src);
}

/**
 * Remove source from main loop
 */
static inline wyrm_error wyrm_main_loop_remove(wyrm_main_loop_ref self, wyrm_primitive src)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->remove(self, src);
}

/**
 * Request iteration of main loop
 */
static inline wyrm_error wyrm_main_loop_iterate(wyrm_main_loop_ref self, bool may_block)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->iterate(self, may_block);
}

/**
 * Run the main loop
 */
static inline wyrm_error wyrm_main_loop_run(wyrm_main_loop_ref self)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->run(self);
}

/**
 * Quit the main loop
 */
static inline wyrm_error wyrm_main_loop_quit(wyrm_main_loop_ref self)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->quit(self);
}

#ifdef __cplusplus
}
#endif

#endif
