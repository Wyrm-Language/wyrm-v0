#ifndef WYRM_WGC_H_
#define WYRM_WGC_H_

#include <wyrm/core.h>
#include <wyrm/types.h>
#include <wyrm/object.h>

WYRM_BEGIN_DECLS

typedef struct wyrm_gc_arena
{
    wyrm_allocator* allocator;
    wyrm_object* first;
    wyrm_object* last;
} wyrm_gc_arena;

void wyrm_gc_init_f(wyrm_gc_arena* self, wyrm_allocator* allocator);
void wyrm_gc_finalize_f(wyrm_context* parent, wyrm_gc_arena* self);

void* wyrm_gc_alloc(wyrm_gc_arena* arena, wyrm_uword dsize);
void* wyrm_gc_realloc(wyrm_gc_arena* arena, void* ptr, wyrm_uword new_size);
void wyrm_gc_free(wyrm_gc_arena* arena, void* ptr);

void wyrm_gc_track(wyrm_gc_arena* context, wyrm_object* gc_info);

void wyrm_gc_collect_start_f(wyrm_context* parent, wyrm_gc_arena* self);
void wyrm_gc_object_visit(wyrm_state* state, wyrm_object* parent);
void wyrm_gc_collect_finish_f(wyrm_context* parent, wyrm_gc_arena* self);

WYRM_END_DECLS

#endif
