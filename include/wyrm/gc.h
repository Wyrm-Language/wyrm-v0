#ifndef WYRM_WGC_H_
#define WYRM_WGC_H_

#include <wyrm/fwd.h>
#include <wyrm/object.h>

WY_BEGIN_DECLS

typedef struct wy_gc_arena
{
    wy_allocator* allocator;
    wy_object* first;
    wy_object* last;
    /** Set by wy_gc_collect_finish_f: objects the sweep examined, and how many of them survived */
    wy_uword last_examined;
    wy_uword last_survivors;
} wy_gc_arena;

void wy_gc_init_f(wy_gc_arena* self, wy_allocator* allocator);
void wy_gc_finalize_f(wy_context* parent, wy_gc_arena* self);

void* wy_gc_alloc(wy_gc_arena* arena, wy_uword dsize);
void* wy_gc_realloc(wy_gc_arena* arena, void* ptr, wy_uword new_size);
void wy_gc_free(wy_gc_arena* arena, void* ptr);

void wy_gc_track(wy_gc_arena* context, wy_object* gc_info);

void wy_gc_collect_start_f(wy_context* parent, wy_gc_arena* self);

/**
 * Mark `parent` and everything reachable from it
 *
 * Iterative (gray worklist), not recursive: a stack of pending objects is
 * grown via the context's allocator as marking proceeds. If the worklist
 * cannot grow (allocator failure), the collection in progress is abandoned
 * - `wy_context_gc_full_run` skips the sweep for that cycle rather than
 * freeing objects this visit never reached, which would free live objects.
 *
 * @param context Context whose current cycle this visit belongs to
 * @param parent Root object to mark, may be WY_NULL (no-op)
 */
void wy_gc_object_visit(wy_context* context, wy_object* parent);
void wy_gc_collect_finish_f(wy_context* parent, wy_gc_arena* self);

WY_END_DECLS

#endif
