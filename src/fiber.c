#include <wyrm.h>
#include <wyrm/fiber.h>
#include <wyrm/stack.h>
#include <wyrm/work_area.h>

static void finalize_f(wy_context* context, wy_object* object);
static wy_error start_children_iter(wy_context* context, wy_object* object, wy_work_area* wa);
static wy_error next_children_iter(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child);

static wy_error fiber_continue_with_return(wy_fiber* self)
{
    /* Not completed executing current task */
    if (self->pending != WY_NULL) { return WY_ERR_INVAL; }

    wy_error last_error = wy_fiber_pop_continuation_f(self, &self->pending);

    /* No continuation, fine - just return */
    if (last_error == WY_ERR_EMPTY) {
        last_error = WY_ERR_NONE;
    }

    return last_error;
}

/**
 * Create a fiber
 */
wy_fiber* wy_fiber_create(wy_context* context, wy_uword stack_len, wy_uword frame_count)
{
    WY_ASSERT(context != WY_NULL);
    if (stack_len == 0 || frame_count == 0) { return WY_NULL; }

    wy_fiber* fiber = wy_context_gc_alloc(context, sizeof(wy_fiber));
    if (fiber == WY_NULL) { return WY_NULL; }

    wy_value* stack = wy_context_gc_alloc(context, stack_len * sizeof(wy_value));
    if (stack == WY_NULL) {
        wy_context_gc_free(context, fiber);
        return WY_NULL;
    }

    /* One extra slot holds the sentinel frame that marks "no active frame". */
    wy_mem_info_init_empty_s(&fiber->frame_memory);
    if (WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, &fiber->frame_memory, frame_count + 1, wy_fiber_frame) != WY_ERR_NONE) {
        wy_context_gc_free(context, stack);
        wy_context_gc_free(context, fiber);
        return WY_NULL;
    }

    wy_stack_init_f(&fiber->value_stack, stack, stack_len);

    fiber->parent = WY_NULL;
    fiber->pending = WY_NULL;

    fiber->current_frame = WY_MEM_INFO_BEGIN_PTR(wy_fiber_frame, &fiber->frame_memory);
    fiber->current_frame->restore_base = wy_stack_base_f(&fiber->value_stack);
    fiber->current_frame->fn = WY_NULL;
    fiber->current_frame->result_count = 0;

    wy_context_object_init_header_f(context, &fiber->object, &wy_type_fiber);
    return fiber;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    wy_fiber* self = (wy_fiber*) object;

    wy_context_gc_free(context, self->value_stack.entries_begin);
    self->value_stack.entries_begin = WY_NULL;
    self->value_stack.entries_end = WY_NULL;
    self->value_stack.base = WY_NULL;
    self->value_stack.top = WY_NULL;

    wy_context_mem_release_f(context, &self->frame_memory);
    self->current_frame = WY_NULL;
}



wy_error wy_fiber_exec_f(wy_fiber* self, wy_context* context)
{
    WY_ASSERT(self != WY_NULL && context != WY_NULL && wy_context_get_fiber_f(context) == self);
    wy_error last_error = WY_ERR_NONE;

    // Continue execution through continuation stack if no forward stack present
    if (self->pending == WY_NULL) {
        last_error = fiber_continue_with_return(self);
    }

    if (last_error != WY_ERR_NONE || self->pending == WY_NULL) {
        return last_error;
    }

    while (last_error == WY_ERR_NONE && self->pending != WY_NULL) {
        wy_exec_fn pending = self->pending;
        self->pending = WY_NULL;

        wy_exec_state result = pending(context);

        switch (result) {
        case WY_EXEC_TAIL_CALL:
            /* wy_fiber_tail_call_f already rebuilt the frame's arguments. */
            if (self->pending == WY_NULL) {
                last_error = WY_ERR_INVAL;
            }
            break;

        case WY_EXEC_CONTINUE:
            if (self->pending == WY_NULL) {
                last_error = WY_ERR_INVAL;
            }
            break;

        case WY_EXEC_DONE:
            if (self->pending != WY_NULL) {
                last_error = WY_ERR_INVAL;
                break;
            }

            last_error = fiber_continue_with_return(self);
            break;

        default:
            last_error = WY_ERR_INVAL;
        }
    }
    return last_error;
}


static wy_error start_children_iter(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = 0;
    return WY_ERR_NONE;
}

static wy_error next_children_iter(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_fiber* self = (wy_fiber*) object;
    wy_word idx = wa->data[0].word;
    while ((self->value_stack.entries_begin + idx) < self->value_stack.top) {
        wy_value* cur = self->value_stack.entries_begin + idx;
        idx++;

        if (wy_value_is_gc_ref_f(*cur)) {
            *child = cur->data.gc_object;
            wa->data[0].word = idx;
            return WY_ERR_NONE;
        }
    }

    wa->data[0].word = idx;
    return WY_ERR_STOP_ITERATION;
}


/**
 * Push a call frame, reserving the caller's result slots
 *
 * @memberof wy_fiber
 */
wy_error wy_fiber_push_frame_f(wy_fiber* self, wy_exec_fn fn, wy_uword result_count)
{
    WY_ASSERT(self != WY_NULL && fn != WY_NULL);

    wy_fiber_frame* new_frame = self->current_frame + 1;
    if (!WY_MEM_INFO_TOP_NOT_AT_END(wy_fiber_frame, new_frame, &self->frame_memory)) {
        return WY_ERR_STACK_OVERFLOW;
    }

    /* Results live below the base, so they are reserved before rebasing. */
    wy_error last_error = wy_stack_reserve_f(&self->value_stack, result_count);
    if (last_error != WY_ERR_NONE) { return last_error; }

    new_frame->fn = fn;
    new_frame->restore_base = wy_stack_base_f(&self->value_stack);
    new_frame->result_count = result_count;

    wy_stack_base_restore_f(&self->value_stack, wy_stack_top_f(&self->value_stack));
    self->current_frame = new_frame;
    return WY_ERR_NONE;
}


/**
 * Pop a call frame, leaving its results on the caller's stack
 *
 * @memberof wy_fiber
 */
wy_error wy_fiber_pop_continuation_f(wy_fiber* self, wy_exec_fn* out_continuation)
{
    WY_ASSERT(self != WY_NULL && out_continuation != WY_NULL);

    if (wy_fiber_frame_depth_f(self) == 0) { return WY_ERR_EMPTY; }

    wy_fiber_frame* frame = self->current_frame;

    /* Drop arguments and scratch; the reserved results sit just below base. */
    self->value_stack.top = self->value_stack.base;
    wy_stack_base_restore_f(&self->value_stack, frame->restore_base);

    *out_continuation = frame->fn;
    self->current_frame--;
    return WY_ERR_NONE;
}


/**
 * Reuse the active frame for a call to `fn`
 *
 * Moves the top `arg_count` values down to the base as the new arguments and
 * makes `fn` pending. The frame keeps its continuation and its reserved
 * result slots, so `fn` returns to whoever the current call would have and
 * writes into the same slots.
 */
wy_error wy_fiber_tail_call_f(wy_fiber* self, wy_exec_fn fn, wy_uword arg_count)
{
    WY_ASSERT(self != WY_NULL);
    if (fn == WY_NULL) { return WY_ERR_INVAL; }
    if (self->pending != WY_NULL) { return WY_ERR_BUSY; }

    wy_error last_error = wy_stack_replace_frame_f(&self->value_stack, arg_count);
    if (last_error != WY_ERR_NONE) { return last_error; }

    self->pending = fn;
    return WY_ERR_NONE;
}


const wy_object_type wy_type_fiber = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_FIBER,

    .finalize = finalize_f,
    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
