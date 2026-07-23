#ifndef WYRM_INTERNAL_API_H_
#define WYRM_INTERNAL_API_H_

#include <wyrm/types.h>
#include <wyrm/sys/string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Primitive
// ----------------------------------------------------------------------------

#define WYRM_PRIMITIVE_PTR(dtype, v) ((dtype*) (v).ptr)

static inline wyrm_primitive wyrm_primitive_null(void) { const wyrm_primitive v = {.ptr = WYRM_NULL}; return v; }
static inline wyrm_primitive wyrm_primitive_int(wyrm_word value) { const wyrm_primitive v = {.word = value}; return v; }
static inline wyrm_primitive wyrm_primitive_ptr(void* value) { const wyrm_primitive v = {.ptr = value}; return v; }


// ----------------------------------------------------------------------------
// Utility Math Functions
// ----------------------------------------------------------------------------

/**
 * Get the next array size given current capacity and initial capacity
 */
WYRM_INLINE wyrm_uword wyrm_next_array_capacity(wyrm_uword current_capacity, wyrm_uword initial)
{
    if (current_capacity >= WYRM_UWORD_HALF) return WYRM_UWORD_MAX;
    return current_capacity == 0 ? initial : current_capacity * 2;
}



// ----------------------------------------------------------------------------
// Integer
// ----------------------------------------------------------------------------

static inline wyrm_value wyrm_make_int(wyrm_word value)
{
    wyrm_value v;
    v.data.word = value;
    v.type = WYRM_TYPE_TAG_WORD;
    return v;
}

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
static inline wyrm_uword wyrm_stack_capacity_remaining_f(wyrm_stack* self)
{
    return self->entries_end - self->top;
}

/**
 * Get total argument on stack
 */
static inline wyrm_uword wyrm_stack_arg_count_f(wyrm_stack* self)
{
    return (wyrm_uword)(self->top - self->base);
}


/**
 * Push value to the stack
 *
 * @param self Stack object
 * @param type_ref Type of primitive being pushed
 * @param value Primitive value
 * @return WYRM_ERR_NONE on success, WYRM_ERR_INVAL if self is NULL, WYRM_ERR_NOMEM if stack is full
 */
static inline wyrm_error wyrm_stack_push_f(wyrm_stack* self, wyrm_type_tag type_ref, wyrm_primitive value)
{
    if (self->top >= self->entries_end) { return WYRM_ERR_NOMEM; }
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
static inline wyrm_error wyrm_stack_pop(wyrm_stack* self, wyrm_type_tag* type_ref, wyrm_primitive* value)
{
    if (self == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->top <= self->entries_begin) { return WYRM_ERR_EMPTY; }
    self->top--;
    if (type_ref != WYRM_NULL) { *type_ref = self->top->type; }
    if (value != WYRM_NULL)    { *value    = self->top->data; }
    return WYRM_ERR_NONE;
}

/**
 * Push continuation to the stack.
 *
 * Set the next continuation for the next set of results. The base pointer
 * marks the space where arguments to the continuation are stored.
 *
 * @memberof wyrm_stack
 *
 * @param self Stack
 * @param cont_fn Function to invoke when this frame's result is ready (may be NULL)
 * @return WYRM_ERR_NONE on success, WYRM_ERR_INVAL if self is NULL, WYRM_ERR_NOMEM if full
 */
static inline wyrm_error wyrm_stack_push_continuation_f(wyrm_stack* self, wyrm_exec_fn cont_fn)
{
    wyrm_value* saved_base = self->top;
    if (saved_base >= self->entries_end) { return WYRM_ERR_NOMEM; }

    wyrm_value* cont = saved_base + 1;
    if (cont >= self->entries_end) { return WYRM_ERR_NOMEM; }

    // base[-2]: saved base pointer
    saved_base->type = WYRM_TYPE_TAG_VALUE_PTR;
    saved_base->data.value_ptr = self->base;

    // base[-1]: continuation
    cont->type = WYRM_TYPE_TAG_FRAME;
    cont->data.cb = cont_fn;

    self->top  = cont + 1;
    self->base = self->top;
    return WYRM_ERR_NONE;
}


/**
 * Pop a stack frame, keeping top N values at the stack top.
 *
 * Restores the base pointer saved by wyrm_stack_push_continuation_f, then
 * moves preserve_count values from the top of the current frame to where the
 * saved-base-pointer slot was, collapsing the frame.
 *
 * @memberof wyrm_stack
 *
 * @param self           Stack
 * @param preserve_count Number of top-of-frame values to keep as results
 * @return WYRM_ERR_NONE on success
 *         WYRM_ERR_INVAL if top stack values invalid
 *         WYRM_ERR_RANGE if current frame holds fewer than preserve_count values
 */
static inline wyrm_error wyrm_stack_pop_continuation_f(wyrm_stack* self, wyrm_exec_fn* out_continuation, wyrm_uword preserve_count)
{
    WYRM_ASSERT(self != WYRM_NULL && out_continuation != WYRM_NULL);
    if ((wyrm_uword)(self->top - self->base) < preserve_count) { return WYRM_ERR_RANGE; }
    if (self->base <= self->entries_begin) { return WYRM_ERR_EMPTY; }
    wyrm_value* cont = self->base - 1;
    if (cont <= self->entries_begin) { return WYRM_ERR_EMPTY; }
    wyrm_value* old_base_value = cont - 1;

    // Verify expected type and save (stack corruption otherwise)
    if (old_base_value->type != WYRM_TYPE_TAG_VALUE_PTR) { return WYRM_ERR_INVAL; }
    wyrm_value* old_base = old_base_value->data.value_ptr;

    if (cont->type != WYRM_TYPE_TAG_FRAME) { return WYRM_ERR_INVAL; }
    *out_continuation = cont->data.cb;

    // Grab base to restore and find continuation
    wyrm_memmove(old_base_value, self->top - preserve_count, preserve_count * sizeof(wyrm_value));
    self->top  = old_base_value + preserve_count;
    self->base = old_base;
    return WYRM_ERR_NONE;
}

/**
 * Replace the top of the stack.
 * @memberof wyrm_stack
 */
static inline wyrm_error wyrm_stack_replace_frame_f(wyrm_stack* self, wyrm_uword preserve_count)
{
    if ((wyrm_uword)(self->top - self->base) < preserve_count) { return WYRM_ERR_RANGE; }
    wyrm_memmove(self->base, self->top - preserve_count, preserve_count * sizeof(wyrm_value));
    self->top = self->base + preserve_count;
    return WYRM_ERR_NONE;
}

/**
 * Push array of values to the stack.
 * @memberof wyrm_stack
 */
static inline wyrm_error wyrm_stack_push_array_f(wyrm_stack* self, wyrm_value* array, wyrm_uword count)
{
    if (count == 0) { return WYRM_ERR_NONE; }
    if ((wyrm_uword)(self->entries_end - self->top) < count) { return WYRM_ERR_NOMEM; }
    wyrm_memmove(self->top, array, count * sizeof(wyrm_value));
    self->top += count;
    return WYRM_ERR_NONE;
}

// ----------------------------------------------------------------------------
// Fiber Startup
// ----------------------------------------------------------------------------

static inline wyrm_uword wyrm_fiber_value_count_f(wyrm_fiber* self)
{
    return wyrm_stack_arg_count_f(&self->value_stack);
}

static inline wyrm_value* wyrm_fiber_value_n(wyrm_fiber* self, wyrm_uword index)
{
    return &self->value_stack.base[index];
}

static inline wyrm_error wyrm_fiber_push_value_f(wyrm_fiber* self, wyrm_value value)
{
    return wyrm_stack_push_f(&self->value_stack, value.type, value.data);
}

static inline wyrm_error wyrm_fiber_push_continuation(wyrm_fiber* self, wyrm_exec_fn fn, wyrm_value* arg, wyrm_uword arg_count)
{
    wyrm_error last_error = wyrm_stack_push_continuation_f(&self->value_stack, fn);
    if (last_error != WYRM_ERR_NONE) { return last_error; }

    last_error = wyrm_stack_push_array_f(&self->value_stack, arg, arg_count);
    if (last_error != WYRM_ERR_NONE) {
        wyrm_exec_fn garbage;
        wyrm_stack_pop_continuation_f(&self->value_stack, &garbage, 0);
        return last_error;
    }

    return WYRM_ERR_NONE;
}

// ----------------------------------------------------------------------------
// Main Loop
// ----------------------------------------------------------------------------

/**
 * Add fd watcher to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_fd(wyrm_main_loop* ref, wyrm_primitive *out, wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud)
{
    if (ref == WYRM_NULL || ref->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return ref->vt->add_fd(ref, out, fd, events, priority, cb, ud);
}

/**
 * Add timer to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_timer(wyrm_main_loop* self, wyrm_primitive *out, uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_timer(self, out, ms, priority, cb, ud);
}

/**
 * Add idle handler to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_idle(wyrm_main_loop* self, wyrm_primitive *out, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_idle(self, out, cb, ud);
}

/**
 * Add wakeable handler to the main loop
 */
static inline wyrm_error wyrm_main_loop_add_wakeable(wyrm_main_loop* self, wyrm_primitive *out, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->add_wakeable(self, out, priority, cb, ud);
}

/**
 * Add triggerable to the main loop
 */
static inline wyrm_error wyrm_main_loop_trigger(wyrm_main_loop* self, wyrm_primitive src)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->trigger(self, src);
}

/**
 * Remove source from main loop
 */
static inline wyrm_error wyrm_main_loop_remove(wyrm_main_loop* self, wyrm_primitive src)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->remove(self, src);
}

/**
 * Request iteration of main loop
 */
static inline wyrm_error wyrm_main_loop_iterate(wyrm_main_loop* self, bool may_block)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->iterate(self, may_block);
}

/**
 * Run the main loop
 */
static inline wyrm_error wyrm_main_loop_run(wyrm_main_loop* self)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->run(self);
}

/**
 * Quit the main loop
 */
static inline wyrm_error wyrm_main_loop_quit(wyrm_main_loop* self)
{
    if (self == WYRM_NULL || self->vt == WYRM_NULL) { return WYRM_ERR_INVAL; }
    return self->vt->quit(self);
}

#ifdef __cplusplus
}
#endif

#endif
