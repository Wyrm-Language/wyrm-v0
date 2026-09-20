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
    if (!wy_exec_fn_is_empty(&self->pending)) { return WY_ERR_INVAL; }

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
    if (WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, &fiber->frame_memory, frame_count + 1, wy_frame) != WY_ERR_NONE) {
        wy_context_gc_free(context, stack);
        wy_context_gc_free(context, fiber);
        return WY_NULL;
    }

    wy_stack_init_f(&fiber->value_stack, stack, stack_len);

    fiber->parent = WY_NULL;
    fiber->pending = wy_exec_fn_create_empty();
    fiber->fault = wy_value_unset();

    fiber->current_frame = WY_MEM_INFO_BEGIN_PTR(wy_frame, &fiber->frame_memory);
    wy_memset(fiber->current_frame, 0, sizeof(wy_frame));
    fiber->current_frame->kind = WY_FRAME_NATIVE;
    fiber->current_frame->ret_kind = WY_RET_RESERVED;
    fiber->current_frame->restore_base = wy_stack_base_f(&fiber->value_stack);
    fiber->current_frame->native = wy_exec_fn_create_empty();
    fiber->current_frame->ret_nres = 0;

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
    if (wy_exec_fn_is_empty(&self->pending)) {
        last_error = fiber_continue_with_return(self);
    }

    if (last_error != WY_ERR_NONE || wy_exec_fn_is_empty(&self->pending)) {
        return last_error;
    }

    while (last_error == WY_ERR_NONE && !wy_exec_fn_is_empty(&self->pending)) {
        wy_exec_fn pending = self->pending;
        self->pending = wy_exec_fn_create_empty();

        wy_exec_state result = pending.fn(context, pending.c_data);

        switch (result) {
        case WY_EXEC_TAIL_CALL:
            /* wy_fiber_tail_call_f already rebuilt the frame's arguments. */
            if (wy_exec_fn_is_empty(&self->pending)) {
                last_error = WY_ERR_INVAL;
            }
            break;

        case WY_EXEC_CONTINUE:
            if (wy_exec_fn_is_empty(&self->pending)) {
                last_error = WY_ERR_INVAL;
            }
            break;

        case WY_EXEC_DONE:
            if (!wy_exec_fn_is_empty(&self->pending)) {
                last_error = WY_ERR_INVAL;
                break;
            }

            last_error = fiber_continue_with_return(self);
            break;

        case WY_EXEC_SWITCH:
            /* The callable already moved context->current_fiber elsewhere
             * and left this fiber suspended, so its pending must stay
             * empty - there is no continuation to chain here, unlike
             * TAIL_CALL/CONTINUE. The while condition below then exits
             * the loop without touching the frame stack. */
            if (!wy_exec_fn_is_empty(&self->pending)) {
                last_error = WY_ERR_INVAL;
            }
            break;

        case WY_EXEC_FAULT:
            /* The callable already set self->fault before returning this. */
            last_error = WY_ERR_FAULT;
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
    wa->data[0].word = 0;   /* value stack index */
    wa->data[1].word = 0;   /* frame index */
    wa->data[2].word = 0;   /* fault visited */
    return WY_ERR_NONE;
}

/**
 * Children: the live value stack, each live frame's armed defer chain (a
 * frame draining defers keeps the rest of its chain only here), then the
 * fault - which must survive the defers that run while a frame unwinds.
 */
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

    if (self->current_frame != WY_NULL) {
        wy_frame* frames = WY_MEM_INFO_BEGIN_PTR(wy_frame, &self->frame_memory);
        wy_word fidx = wa->data[1].word;
        while (frames + fidx <= self->current_frame) {
            wy_frame* fr = frames + fidx;
            fidx++;
            if (fr->defers != WY_NULL) {
                *child = (wy_object*) fr->defers;
                wa->data[1].word = fidx;
                return WY_ERR_NONE;
            }
        }
        wa->data[1].word = fidx;
    }

    if (wa->data[2].word == 0) {
        wa->data[2].word = 1;
        if (wy_value_is_gc_ref_f(self->fault)) {
            *child = self->fault.data.gc_object;
            return WY_ERR_NONE;
        }
    }
    return WY_ERR_STOP_ITERATION;
}


/**
 * Push a call frame, reserving the caller's result slots
 *
 * @memberof wy_fiber
 */
wy_error wy_fiber_push_frame_f(wy_fiber* self, wy_exec_fn fn, wy_uword result_count)
{
    WY_ASSERT(self != WY_NULL && !wy_exec_fn_is_empty(&fn));

    wy_frame* new_frame = self->current_frame + 1;
    if (!WY_MEM_INFO_TOP_NOT_AT_END(wy_frame, new_frame, &self->frame_memory)) {
        return WY_ERR_STACK_OVERFLOW;
    }

    /* Results live below the base, so they are reserved before rebasing. */
    wy_error last_error = wy_stack_reserve_f(&self->value_stack, result_count);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_memset(new_frame, 0, sizeof(wy_frame));
    new_frame->kind = WY_FRAME_NATIVE;
    new_frame->ret_kind = WY_RET_RESERVED;
    new_frame->native = fn;
    new_frame->restore_base = wy_stack_base_f(&self->value_stack);
    new_frame->ret_nres = (wy_u16) result_count;

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

    wy_frame* frame = self->current_frame;

    /* Drop arguments and scratch; the reserved results sit just below base. */
    self->value_stack.top = self->value_stack.base;
    wy_stack_base_restore_f(&self->value_stack, frame->restore_base);

    *out_continuation = frame->native;
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
    if (wy_exec_fn_is_empty(&fn)) { return WY_ERR_INVAL; }
    if (!wy_exec_fn_is_empty(&self->pending)) { return WY_ERR_BUSY; }

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
