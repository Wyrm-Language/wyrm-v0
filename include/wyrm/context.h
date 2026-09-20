#ifndef WYRM_CONTEXT_H_
#define WYRM_CONTEXT_H_

#include <wyrm/fwd.h>
#include <wyrm/fiber.h>
#include <wyrm/gc.h>
#include <wyrm/mem_info.h>

/* ------------------------------------------------------------------------- */
/* Context API                                                               */
/* ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

/** Initial capacity reserved for the module list on first registration */
#define WY_CONTEXT_MODULE_INITIAL 4

/** Depth of the root stack C code can use to protect fresh objects across allocations */
#define WY_CONTEXT_ROOT_STACK_LEN 64

/** Default gc_pressure threshold (bytes) before a safepoint triggers a collection */
#define WY_CONTEXT_GC_THRESHOLD_DEFAULT (1u << 16)

/** Default coroutine fiber sizing (design_c_vm.md §3). */
#define WY_CONTEXT_CO_STACK_LEN_DEFAULT 512
#define WY_CONTEXT_CO_FRAME_COUNT_DEFAULT 32

/**
 * Output sink for builtins like `print`/`println` (design_c_vm.md §5).
 * `write` may be WY_NULL, in which case output is silently dropped; the
 * hosted platform port defaults it to stdout (src/platform/hosted).
 */
typedef void (*wy_io_write_fn)(wy_context* context, const char* bytes, wy_uword len, void* ud);
typedef struct wy_context_io
{
    wy_io_write_fn write;
    void* ud;
} wy_context_io;

/** The hook returns an image allocated through wy_context_gc_alloc. On
 * success ownership transfers to the loader (also on malformed-image failure).
 * On failure the hook retains responsibility for any allocation. */
typedef wy_error (*wy_import_hook)(wy_context*, const char* path, wy_uword len,
    wy_u8** out_bytes, wy_uword* out_len, void* ud);

struct wy_context
{
    wy_machine* parent;
    wy_fiber* current_fiber;
    wy_module* root_module;
    wy_module* builtins;       /**< the builtins module; NULL until epic 2/M5 installs it */
    wy_class* error_class;     /**< the base `error` class; NULL until wy_builtins_new installs it */
    wy_class* stop_iteration_class;  /**< the `StopIteration` class; NULL until wy_builtins_new installs it */
    wy_class* os_error_class;  /**< the `OSError` class; NULL until wy_builtins_new installs it */
    wy_fiber* fiber_list;      /**< intrusive list of root/independent fibers (design_c_vm.md §3's
                                 * "context's fiber list"); a coroutine's own private fiber is never
                                 * linked here, only reachable via its owning wy_coroutine - see
                                 * wy_context_gc_full_run and wy_context_attach_fiber */
    wy_uword co_stack_len;     /**< default value-stack length for a coroutine's own fiber */
    wy_uword co_frame_count;   /**< default frame count for a coroutine's own fiber */
    wy_main_loop* main_loop;
    wy_primitive wakeable_source;
    wy_gc_arena arena;
    wy_context_io io;
    wy_import_hook import_hook;
    void* import_ud;

    wy_mem_info module_memory;
    wy_uword module_count;

    /** Bytes allocated since the last collection; compared against gc_threshold at safepoints */
    wy_uword gc_pressure;
    wy_uword gc_threshold;
    /** Set mid-collection when the mark worklist could not grow; the sweep for that cycle is skipped */
    bool gc_abandoned;

    /** Fixed root stack: C code holding a fresh object across further allocations pushes its slot here */
    wy_value* roots[WY_CONTEXT_ROOT_STACK_LEN];
    wy_uword root_count;
};

void wy_context_init_s(wy_context* self);
void wy_context_finalize_f(wy_context* self);

wy_error wy_context_set_root(wy_context* context, wy_module* module);

wy_error wy_context_module_register(wy_context* self, wy_module* module, wy_uword* out_module_id);

/**
 * Intern `len` bytes of UTF-8 as a symbol: two interned strings compare
 * pointer-identical (`wy_symbol` is compared by pointer) iff their first 31
 * UTF-8 codepoints are byte-identical (see wyc-format.md §8.4 and
 * wy_symtab_intern). Loader code MUST go through this one entry point
 * rather than the machine's wy_symtab directly.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM on
 *   allocation failure
 */
wy_error wy_context_intern(wy_context* context, const char* text, wy_uword len, wy_symbol* out);

wy_error wy_context_attach_loop(wy_context* context, wy_main_loop* loop);
void wy_context_detach_loop(wy_context* context);

wy_error wy_context_activate(wy_context* self, wy_fiber* fiber);
wy_error wy_context_attach_fiber(wy_context* self, wy_fiber* fiber);

wy_error wy_context_exec(wy_context* self);

void wy_ctx_object_init_header_static_f(wy_context* context, wy_object* object, const wy_object_type* dtype);

WY_INLINE wy_machine* wy_context_get_machine(wy_context* self);

void wy_context_object_init_header_f(wy_context* context, wy_object* object, const wy_object_type* dtype);

void* wy_context_gc_alloc(wy_context* context, wy_uword dsize);
void* wy_context_gc_realloc(wy_context* context, void* ptr, wy_uword new_size);
void wy_context_gc_free(wy_context* context, void* ptr);
void wy_context_push_gc(wy_context* context, wy_object* gc_info);

void wy_context_gc_full_run(wy_context* context);

/**
 * Collect if allocation pressure has crossed the threshold since the last
 * collection; a no-op otherwise. Called between VM instructions (a
 * safepoint), never mid-instruction, so instruction handlers need no
 * handles for values they haven't yet rooted.
 */
void wy_context_gc_safepoint(wy_context* context);

/**
 * Push `slot`'s current value onto the root stack, so it survives any
 * collection triggered by further allocation until popped
 *
 * For C code (loader, builtins, natives) that allocates more than once in
 * a row before the first result is reachable from any other root.
 *
 * @return WY_ERR_NONE, or WY_ERR_STACK_OVERFLOW if the root stack is full
 */
wy_error wy_context_root_push_f(wy_context* context, wy_value* slot);

/** Pop the most recently pushed root. No-op if the root stack is empty. */
void wy_context_root_pop_f(wy_context* context);


#define WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, mem_info, count, type) (wy_context_mem_reserve_count_f((context), (mem_info), (count), sizeof(type)))

wy_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz);
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info);

/**
 * Grow `mem_info` to hold at least `count` blocks of `block_sz` bytes
 */
WY_INLINE wy_error wy_context_mem_reserve_count_f(wy_context* context, wy_mem_info* mem_info, wy_uword count, wy_uword block_sz)
{
    if (count > WY_MAX_ARRAY_LEN) { return WY_ERR_INVAL; }
    wy_uword mem_sz = wy_mem_info_block_count_sz_f(block_sz, count);
    return wy_context_mem_reserve_f(context, mem_info, mem_sz);
}


/**
 * Gab machine associated with context
 *
 * @param self Context
 * @return Machine associated with the context
 */
WY_INLINE wy_machine* wy_context_get_machine(wy_context* self)
{
    if (!self) { return WY_NULL; }
    return self->parent;
}


/**
 * Get the fiber the context is currently running
 */
WY_INLINE wy_fiber* wy_context_get_fiber_f(wy_context* self)
{
    if (self == WY_NULL) { return WY_NULL; }
    return self->current_fiber;
}

/**
 * Count the values visible to the current call
 * @return Number of values in the active frame
 */
WY_INLINE wy_uword wy_context_value_count(wy_context* self)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return 0; }
    return wy_fiber_value_count_f(self->current_fiber);
}

/**
 * Discard values until the active frame holds `count` of them
 * @return WY_ERR_NONE on success, WY_ERR_RANGE if the frame holds fewer
 */
WY_INLINE wy_error wy_context_pop_to_value_count(wy_context* self, wy_uword count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_pop_to_value_count_f(self->current_fiber, count);
}

/**
 * Access a value in the active frame
 * @return Pointer to the value, or WY_NULL when out of range
 */
WY_INLINE wy_value* wy_context_value_n(wy_context* self, wy_uword index)
{
    if (index >= wy_context_value_count(self)) { return WY_NULL; }
    return wy_fiber_value_n(self->current_fiber, index);
}

/**
 * Push a value onto the current fiber's stack
 */
WY_INLINE wy_error wy_context_push(wy_context* self, wy_value value)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_push_value_f(self->current_fiber, value);
}

/**
 * Set the callable the current fiber runs next
 */
WY_INLINE wy_error wy_context_set_pending(wy_context* self, wy_exec_fn pending)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (wy_exec_fn_is_empty(&pending)) { return WY_ERR_INVAL; }
    if (!wy_exec_fn_is_empty(&self->current_fiber->pending)) { return WY_ERR_BUSY; }
    self->current_fiber->pending = pending;
    return WY_ERR_NONE;
}

/**
 * Set the C function the current fiber runs next
 *
 * @see wy_context_set_pending
 */
WY_INLINE wy_error wy_context_set_pending_c_call(wy_context* self, wy_exec_fn_c_call pending)
{
    if (pending == WY_NULL) { return WY_ERR_INVAL; }
    return wy_context_set_pending(self, wy_exec_fn_create(pending, wy_primitive_null()));
}

/**
 * Get the number of results the caller reserved for this call
 */
WY_INLINE wy_uword wy_context_result_count(wy_context* self)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return 0; }
    return wy_fiber_result_count_f(self->current_fiber);
}

/**
 * Access a reserved result slot of the current call
 */
WY_INLINE wy_value* wy_context_result_n(wy_context* self, wy_uword index)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_NULL; }
    return wy_fiber_result_n(self->current_fiber, index);
}

/**
 * Store result `result` of the current call, truncating if unreserved
 */
WY_INLINE bool wy_context_set_result(wy_context* self, wy_uword index, wy_value value)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return false; }
    return wy_fiber_set_result_f(self->current_fiber, index, value);
}

/**
 * Reuse the current call's frame to call `fn`
 *
 * The top `arg_count` values become the arguments. `fn` inherits this call's
 * reserved return slots and the written count is reset.
 */
WY_INLINE wy_error wy_context_tail_call(wy_context* self, wy_exec_fn_c_call fn, wy_uword arg_count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_tail_call_c_call_f(self->current_fiber, fn, arg_count);
}

/**
 * Reuse the current call's frame to call the callable `fn`
 *
 * @see wy_context_tail_call
 */
WY_INLINE wy_error wy_context_tail_call_exec_fn(wy_context* self, wy_exec_fn fn, wy_uword arg_count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_tail_call_f(self->current_fiber, fn, arg_count);
}

/**
 * Call `fn` with `args`, resuming at `result_cb` with `result_count` results
 *
 * Reserve `result_count` return slots for the function call and value with
 * the given arguments.
 */
WY_INLINE wy_error wy_context_call_continue(wy_context* self, wy_exec_fn_c_call result_cb, wy_exec_fn_c_call fn, const wy_value* args, wy_uword arg_count, wy_uword result_count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (arg_count > 0 && args == WY_NULL) { return WY_ERR_INVAL; }
    return wy_fiber_exec_continue_c_call_f(self->current_fiber, result_cb, fn, args, arg_count, result_count);
}

/**
 * Call the callable `fn`, resuming at `result_cb`
 *
 * @see wy_context_call_continue
 */
WY_INLINE wy_error wy_context_call_continue_exec_fn(wy_context* self, wy_exec_fn result_cb, wy_exec_fn fn, const wy_value* args, wy_uword arg_count, wy_uword result_count)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (arg_count > 0 && args == WY_NULL) { return WY_ERR_INVAL; }
    if (wy_exec_fn_is_empty(&result_cb) || wy_exec_fn_is_empty(&fn)) { return WY_ERR_INVAL; }
    return wy_fiber_exec_continue_f(self->current_fiber, result_cb, fn, args, arg_count, result_count);
}


/**
 * Get the total number of modules
 */
WY_INLINE wy_uword wy_context_module_count(wy_context* self)
{
    if (self == WY_NULL) { return 0; }
    return self->module_count;
}


/**
 * Get module from module index
 */
WY_INLINE wy_module* wy_context_get_module(wy_context* self, wy_uword module_id)
{
    if (self == WY_NULL || module_id >= self->module_count) { return WY_NULL; }
    return WY_MEM_INFO_BEGIN_PTR(wy_module*, &self->module_memory)[module_id];
}


WY_END_DECLS

#endif
