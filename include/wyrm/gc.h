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
} wy_gc_arena;

void wy_gc_init_f(wy_gc_arena* self, wy_allocator* allocator);
void wy_gc_finalize_f(wy_context* parent, wy_gc_arena* self);

void* wy_gc_alloc(wy_gc_arena* arena, wy_uword dsize);
void* wy_gc_realloc(wy_gc_arena* arena, void* ptr, wy_uword new_size);
void wy_gc_free(wy_gc_arena* arena, void* ptr);

void wy_gc_track(wy_gc_arena* context, wy_object* gc_info);

void wy_gc_collect_start_f(wy_context* parent, wy_gc_arena* self);
void wy_gc_object_visit(wy_state* state, wy_object* parent);
void wy_gc_collect_finish_f(wy_context* parent, wy_gc_arena* self);

WY_END_DECLS

#endif
