#include <wyrm.h>
#include <wyrm/mem_info.h>

/**
 * One or more fibers flagged active and ready.
 *
 * This is triggered when a fiber is ready to be executed. It iterates
 * once on all runnable fibers before returning to the main loop.
 *
 * TODO: We don't currently handle the situation where a fiber isn't
 *       operating correctly. This should wakeup a management thread
 *       and/or flag the status on the context.
 *
 * TODO: We just hardcode current_fiber in the context, instead -
 *       we should have some scheduling/extended logic here to
 *       determine which fiber needs to run. Currently, only
 *       1 fiber, so not an issue.
 *
 * @param ud the wy_context programmed into the main loop directly
 * @return Always returns true; this trigger is active until context
 *         itself is destroyed.
 */
static bool context_triggered(wy_primitive ud)
{
    /* Grab current context */
    wy_context* self = WY_PRIMITIVE_PTR(wy_context, ud);

    /* TODO: determine correct fiber to execute, set self->current_fiber appropriately */
    if (self->current_fiber != WY_NULL) {
        wy_error last_error = wy_context_exec(self);
        if (last_error != WY_ERR_NONE) {
            /* TODO: flag/update context and fiber */
        }
    }
    return true;
}


void wy_context_init_s(wy_context* self)
{
    self->parent = WY_NULL;
    self->root_module = WY_NULL;
    self->builtins = WY_NULL;
    self->error_class = WY_NULL;
    self->current_fiber = WY_NULL;
    self->main_loop = WY_NULL;
    self->wakeable_source = wy_primitive_null();

    wy_mem_info_init_empty_s(&self->module_memory);
    self->module_count = 0;

    self->gc_pressure = 0;
    self->gc_threshold = WY_CONTEXT_GC_THRESHOLD_DEFAULT;
    self->gc_abandoned = false;
    self->root_count = 0;

    self->io.write = WY_NULL;
    self->io.ud = WY_NULL;

    wy_gc_init_f(&self->arena, WY_NULL);
}


void wy_context_finalize_f(wy_context* self)
{
    if (self == WY_NULL) { return; }

    /* Free the wakeable source */
    if (self->main_loop != WY_NULL) {
        wy_context_detach_loop(self);
    }

    wy_context_mem_release_f(self, &self->module_memory);
    self->module_count = 0;

    /* Free all objects */
    wy_gc_finalize_f(self, &self->arena);
}

/**
 * Set the root module associated with the context.
 *
 * A context "runs" via association with a root module. This module is the
 * 'main' entry point for the context. After the module has executed, the
 * context may remain - allowing asynchronous events keyed off an associated
 * main loop if constructed.
 *
 * @param context The context receiving the module.
 * @param module The dynamic module state object
 * @return WY_ERR_NONE on success; WY_ERR_BUSY if root already set.
 */
wy_error wy_context_set_root(wy_context* context, wy_module* module)
{
    if (context->root_module != WY_NULL) { return WY_ERR_BUSY; }
    context->root_module = module;
    return WY_ERR_NONE;
}

/**
 * Register a module and hand back the id that names it
 *
 * Ids are dense and assigned in registration order, so they index the list
 * directly. The list grows by doubling and is capped at the width of the
 * module field in a packed bytecode callable.
 *
 * @param self Context
 * @param module Module to register
 * @param out_module_id Receives the assigned id, may be WY_NULL
 * @return WY_ERR_NONE on success, WY_ERR_RANGE when the id space is full
 */
wy_error wy_context_module_register(wy_context* self, wy_module* module, wy_uword* out_module_id)
{
    if (self == WY_NULL || module == WY_NULL) { return WY_ERR_INVAL; }
    if (self->module_count >= WY_EXEC_FN_MODULE_MAX) { return WY_ERR_RANGE; }

    wy_uword capacity = WY_MEM_INFO_COUNT(wy_module*, &self->module_memory);
    if (self->module_count >= capacity) {
        wy_uword grown = (capacity == 0) ? WY_CONTEXT_MODULE_INITIAL : (capacity * 2);
        if (grown > WY_EXEC_FN_MODULE_MAX) { grown = WY_EXEC_FN_MODULE_MAX; }

        wy_error last_error = WY_CONTEXT_MEM_INFO_RESERVE_COUNT(self, &self->module_memory, grown, wy_module*);
        if (last_error != WY_ERR_NONE) { return last_error; }
    }

    wy_uword module_id = self->module_count;
    WY_MEM_INFO_BEGIN_PTR(wy_module*, &self->module_memory)[module_id] = module;
    self->module_count++;

    if (out_module_id != WY_NULL) { *out_module_id = module_id; }
    return WY_ERR_NONE;
}

wy_error wy_context_attach_loop(wy_context* self, wy_main_loop* loop)
{
    if (self->main_loop != WY_NULL) { return WY_ERR_BUSY; }

    wy_error last_error = wy_main_loop_add_wakeable(
        loop,
        &self->wakeable_source,
        WY_PRIORITY_DEFAULT,
        context_triggered,
        wy_primitive_ptr(self));
    if (last_error == WY_ERR_NONE) {
        self->main_loop = loop;
    }

    return last_error;
}

void wy_context_detach_loop(wy_context* self)
{
    if (self->main_loop) {
        wy_main_loop_remove(self->main_loop, self->wakeable_source);
        self->wakeable_source = wy_primitive_null();
        self->main_loop = WY_NULL;
    }
}


/**
 * Run the context's current fiber until it yields or completes
 *
 * @param self Context with an attached fiber
 * @return WY_ERR_NONE on success, WY_ERR_INVAL when no fiber is attached
 */
wy_error wy_context_exec(wy_context* self)
{
    if (self == WY_NULL || self->current_fiber == WY_NULL) { return WY_ERR_INVAL; }

    /* wy_fiber_exec_f drives one fiber until its own pending queue is
     * empty, which includes the case where a WY_EXEC_SWITCH callable moved
     * self->current_fiber elsewhere. Loop here so a switch resumes on
     * whichever fiber is now current (design_c_vm.md §3; no coroutines
     * exist yet to actually produce a switch, but the loop shape lands
     * with this milestone). Stops once the fiber we just ran is still
     * current, i.e. nothing switched. */
    wy_fiber* last_run = WY_NULL;
    wy_error last_error = WY_ERR_NONE;
    while (self->current_fiber != WY_NULL && self->current_fiber != last_run) {
        last_run = self->current_fiber;
        last_error = wy_fiber_exec_f(last_run, self);
        if (last_error != WY_ERR_NONE) { break; }
    }
    return last_error;
}


wy_error wy_context_attach_fiber(wy_context* self, wy_fiber* fiber)
{
    if (self == WY_NULL || fiber == WY_NULL) { return WY_ERR_INVAL; }
    if (fiber->parent != WY_NULL) { return WY_ERR_BUSY; }
    self->current_fiber = fiber;
    fiber->parent = self;
    return WY_ERR_NONE;
}



wy_error wy_context_activate(wy_context* self, wy_fiber* fiber)
{
    wy_error last_error = WY_ERR_NONE;
    WY_UNUSED(fiber);

    if (self != WY_NULL &&
        self->main_loop != WY_NULL)
    {
        last_error = wy_main_loop_trigger(self->main_loop, self->wakeable_source);
    } else {
        last_error = WY_ERR_INVAL;
    }
    return last_error;
}


wy_error wy_context_intern(wy_context* context, const char* text, wy_uword len, wy_symbol* out)
{
    if (context == WY_NULL || text == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_machine* machine = wy_context_get_machine(context);
    if (machine == WY_NULL) { return WY_ERR_INVAL; }

    wy_symbol interned = wy_symtab_intern(&machine->symtab, text, len);
    if (interned == WY_SYMBOL_INVALID) { return WY_ERR_NOMEM; }

    *out = interned;
    return WY_ERR_NONE;
}


void* wy_context_gc_alloc(wy_context* context, wy_uword dsize)
{
    wy_machine* machine = wy_context_get_machine(context);
    if (machine == WY_NULL) { return WY_NULL; }

    void* ptr = wy_allocator_alloc(machine->allocator, dsize);
    if (ptr != WY_NULL) { context->gc_pressure += dsize; }
    return ptr;
}


void* wy_context_gc_realloc(wy_context* context, void* ptr, wy_uword new_size)
{
    wy_machine* machine = wy_context_get_machine(context);
    if (machine == WY_NULL) { return WY_NULL; }

    return wy_allocator_realloc(machine->allocator, ptr, new_size);
}


void wy_context_gc_free(wy_context* context, void* ptr)
{
    wy_machine* machine = wy_context_get_machine(context);
    if (machine != WY_NULL) {
        wy_allocator_free(machine->allocator, ptr);
    }
}


void wy_context_push_gc(wy_context* context, wy_object* gc_info)
{
    wy_gc_track(&context->arena, gc_info);
}


void wy_context_object_init_header_f(wy_context* context, wy_object* object, const wy_object_type* dtype)
{
    WY_ASSERT(context != WY_NULL && object != WY_NULL && dtype != WY_NULL);
    wy_object_init_header_s(object, dtype);
    wy_context_push_gc(context, object);
}


void wy_context_gc_full_run(wy_context* context)
{
    context->gc_abandoned = false;
    wy_gc_collect_start_f(context, &context->arena);

    if (context->current_fiber != WY_NULL) {
        wy_gc_object_visit(context, (wy_object*) context->current_fiber);
    }

    if (context->root_module != WY_NULL) {
        wy_gc_object_visit(context, (wy_object*) context->root_module);
    }

    if (context->builtins != WY_NULL) {
        wy_gc_object_visit(context, (wy_object*) context->builtins);
    }

    /* Registered modules are reachable by id alone, so they are roots. */
    wy_module** modules = WY_MEM_INFO_BEGIN_PTR(wy_module*, &context->module_memory);
    for (wy_uword i = 0; i < context->module_count; i++) {
        wy_gc_object_visit(context, (wy_object*) modules[i]);
    }

    /* C code holding a fresh value across allocations. */
    for (wy_uword i = 0; i < context->root_count; i++) {
        if (wy_value_is_gc_ref_f(*context->roots[i])) {
            wy_gc_object_visit(context, context->roots[i]->data.gc_object);
        }
    }

    if (context->gc_abandoned) {
        /* A worklist growth failure means some reachable objects were never
         * marked; sweeping now would free live objects, so skip it. Marks
         * are cleared again by the next collect_start regardless. */
        return;
    }

    wy_gc_collect_finish_f(context, &context->arena);
    context->gc_pressure = 0;
}


void wy_context_gc_safepoint(wy_context* context)
{
    if (context == WY_NULL) { return; }
    if (context->gc_pressure > context->gc_threshold) {
        wy_context_gc_full_run(context);
    }
}


wy_error wy_context_root_push_f(wy_context* context, wy_value* slot)
{
    WY_ASSERT(context != WY_NULL && slot != WY_NULL);
    if (context->root_count >= WY_CONTEXT_ROOT_STACK_LEN) { return WY_ERR_STACK_OVERFLOW; }
    context->roots[context->root_count++] = slot;
    return WY_ERR_NONE;
}


void wy_context_root_pop_f(wy_context* context)
{
    WY_ASSERT(context != WY_NULL);
    if (context->root_count > 0) { context->root_count--; }
}

/**
 * Grow `mem_info` to hold at least `sz` bytes
 */
wy_error wy_context_mem_reserve_f(wy_context* context, wy_mem_info* mem_info, wy_uword sz)
{
    WY_ASSERT(context != WY_NULL && mem_info != WY_NULL);

    if (wy_mem_info_sz_f(mem_info) >= sz) { return WY_ERR_NONE; }
    if (wy_mem_info_is_static_f(mem_info)) { return WY_ERR_INVAL; }

    void* begin = wy_context_gc_realloc(context, mem_info->begin, sz);
    if (begin == WY_NULL) { return WY_ERR_NOMEM; }

    mem_info->begin = begin;
    mem_info->end = (void*) ((char*) begin + sz);
    return WY_ERR_NONE;
}

/**
 * Release memory held by `mem_info` and reset it to empty
 */
void wy_context_mem_release_f(wy_context* context, wy_mem_info* mem_info)
{
    WY_ASSERT(context != WY_NULL && mem_info != WY_NULL);

    if (!wy_mem_info_is_static_f(mem_info)) {
        wy_context_gc_free(context, mem_info->begin);
    }
    wy_mem_info_init_empty_s(mem_info);
}
