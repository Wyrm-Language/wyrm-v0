#include <wyrm.h>
#include <wyrm/fiber.h>
#include <wyrm/stack.h>

static void finalize_f(wyrm_context* context, wyrm_object* object);
static wyrm_error start_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa);
static wyrm_error next_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa, const wyrm_object** child);

static wyrm_error fiber_continue_with_return(wyrm_fiber* self)
{
    /* Not completed executing current task */
    if (self->pending != WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_error last_error = wy_fiber_pop_continuation_f(
        self,
        &self->pending,
        self->tail_preserve_count);

    if (last_error == WYRM_ERR_NONE) {
        self->tail_preserve_count = 0;
    }

    /* No continuation, fine - just return */
    if (last_error == WYRM_ERR_EMPTY) {
        last_error = WYRM_ERR_NONE;
    }

    return last_error;
}

/**
 * Create a fiber
 */
wyrm_fiber* wyrm_fiber_create(wyrm_context* context, wyrm_uword stack_len, wy_uword frame_count)
{
    WYRM_ASSERT(context != WYRM_NULL);
    if (stack_len == 0 || frame_count == 0) { return WYRM_NULL; }

    wyrm_fiber* fiber = wyrm_context_gc_alloc(context, sizeof(wyrm_fiber));
    if (fiber == WYRM_NULL) { return WYRM_NULL; }

    wyrm_value* stack = wyrm_context_gc_alloc(context, stack_len * sizeof(wyrm_value));
    if (stack == WYRM_NULL) {
        wyrm_context_gc_free(context, fiber);
        return WYRM_NULL;
    }

    /* One extra slot holds the sentinel frame that marks "no active frame". */
    wy_mem_info_init_empty_s(&fiber->frame_memory);
    if (WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, &fiber->frame_memory, frame_count + 1, wy_fiber_frame) != WYRM_ERR_NONE) {
        wyrm_context_gc_free(context, stack);
        wyrm_context_gc_free(context, fiber);
        return WYRM_NULL;
    }

    wyrm_stack_init_f(&fiber->value_stack, stack, stack_len);

    fiber->parent = WYRM_NULL;
    fiber->pending = WYRM_NULL;
    fiber->tail_preserve_count = 0;

    fiber->current_frame = WY_MEM_INFO_BEGIN_PTR(wy_fiber_frame, &fiber->frame_memory);
    fiber->current_frame->restore_base = wyrm_stack_base_f(&fiber->value_stack);
    fiber->current_frame->fn = WYRM_NULL;

    wyrm_context_object_init_header_f(context, &fiber->object, &wyrm_type_fiber);
    return fiber;
}


static void finalize_f(wyrm_context* context, wyrm_object* object)
{
    wyrm_fiber* self = (wyrm_fiber*) object;

    wyrm_context_gc_free(context, self->value_stack.entries_begin);
    self->value_stack.entries_begin = WYRM_NULL;
    self->value_stack.entries_end = WYRM_NULL;
    self->value_stack.base = WYRM_NULL;
    self->value_stack.top = WYRM_NULL;

    wy_context_mem_release_f(context, &self->frame_memory);
    self->current_frame = WYRM_NULL;
}



wyrm_error wyrm_fiber_exec_f(wyrm_fiber* self, wyrm_state* state)
{
    WYRM_ASSERT(self != WYRM_NULL && state != WYRM_NULL && state->fiber == self);
    wyrm_error last_error = WYRM_ERR_NONE;

    // Continue execution through continuation stack if no forward stack present
    if (self->pending == WYRM_NULL) {
        last_error = fiber_continue_with_return(self);
    }

    if (last_error != WYRM_ERR_NONE || self->pending == WYRM_NULL) {
        return last_error;
    }

    while (last_error == WYRM_ERR_NONE && self->pending != WYRM_NULL) {
        wyrm_exec_fn pending = self->pending;
        self->pending = WYRM_NULL;

        wyrm_exec_state result = pending(state);

        switch (result) {
        case WYRM_EXEC_TAIL_CALL:
            last_error = wyrm_stack_replace_frame_f(&self->value_stack, self->tail_preserve_count);
            if (self->pending == WYRM_NULL) {
                last_error = WYRM_ERR_INVAL;
            } else {
                self->tail_preserve_count = 0;
            }
            break;

        case WYRM_EXEC_CONTINUE:
            if (self->pending == WYRM_NULL) {
                last_error = WYRM_ERR_INVAL;
            }
            break;

        case WYRM_EXEC_DONE:
            if (self->pending != WYRM_NULL) {
                last_error = WYRM_ERR_INVAL;
                break;
            }

            last_error = fiber_continue_with_return(self);
            break;

        default:
            last_error = WYRM_ERR_INVAL;
        }
    }
    return last_error;
}


static wyrm_error start_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa)
{
    WYRM_UNUSED(state); WYRM_UNUSED(object);
    wyrm_memset(wa, 0, sizeof(wyrm_work_area));
    wa->data[0].word = 0;
    return WYRM_ERR_NONE;
}

static wyrm_error next_children_iter(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa, const wyrm_object** child)
{
    WYRM_UNUSED(state);
    wyrm_fiber* self = (wyrm_fiber*) object;
    wyrm_word idx = wa->data[0].word;
    while ((self->value_stack.entries_begin + idx) < self->value_stack.top) {
        wyrm_value* cur = self->value_stack.entries_begin + idx;
        idx++;

        if (wyrm_type_tag_is_gc(cur->type)) {
            *child = cur->data.gc_object;
            wa->data[0].word = idx;
            return WYRM_ERR_NONE;
        }
    }

    wa->data[0].word = idx;
    return WYRM_ERR_STOP_ITERATION;
}


/**
 * Push a call frame
 */
wyrm_error wy_fiber_push_frame_f(wyrm_fiber* self, wyrm_exec_fn fn)
{
    WYRM_ASSERT(self != WYRM_NULL && fn != WYRM_NULL);

    wy_fiber_frame* new_frame = self->current_frame + 1;
    if (!WY_MEM_INFO_TOP_NOT_AT_END(wy_fiber_frame, new_frame, &self->frame_memory)) {
        return WYRM_ERR_STACK_OVERFLOW;
    }

    new_frame->fn = fn;
    new_frame->restore_base = wyrm_stack_base_f(&self->value_stack);

    wyrm_stack_base_restore_f(&self->value_stack, wyrm_stack_top_f(&self->value_stack));
    self->current_frame = new_frame;
    return WYRM_ERR_NONE;
}


/**
 * Pop a call frame, transferring the last N values to the new stack top
 */
wyrm_error wy_fiber_pop_continuation_f(wy_fiber* self, wyrm_exec_fn* out_continuation, wyrm_uword preserve_count)
{
    WYRM_ASSERT(self != WYRM_NULL && out_continuation != WYRM_NULL);

    if (self->current_frame == WY_MEM_INFO_BEGIN_PTR(wy_fiber_frame, &self->frame_memory)) { return WYRM_ERR_EMPTY; }

    wy_error last_error = wy_stack_base_reset_args_f(
        &self->value_stack, self->current_frame->restore_base, preserve_count);
    if (last_error != WYRM_ERR_NONE) { return last_error; }

    *out_continuation = self->current_frame->fn;
    self->current_frame--;
    return WYRM_ERR_NONE;
}


const wyrm_object_type wyrm_type_fiber = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_FIBER,

    .finalize = finalize_f,
    .children_iter_start = start_children_iter,
    .children_iter_next = next_children_iter,
};
