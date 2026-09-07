#include <wyrm/gc.h>
#include <wyrm.h>
#include <wyrm/work_area.h>

void wy_gc_init_f(wy_gc_arena* self, wy_allocator* allocator)
{
    self->first = WY_NULL;
    self->last = WY_NULL;
    self->allocator = allocator;
}


void wy_gc_finalize_f(wy_context* parent, wy_gc_arena* self)
{
    wy_object* next = self->first;
    for (wy_object* cur = next; cur; cur = next) {
        next = cur->next;
        wy_object_finalize_f(parent, cur);
        wy_gc_free(self, cur);
    }
}


void* wy_gc_alloc(wy_gc_arena* arena, wy_uword dsize)
{
    return wy_allocator_alloc(arena->allocator, dsize);
}


void* wy_gc_realloc(wy_gc_arena* arena, void* ptr, wy_uword new_size)
{
    return wy_allocator_realloc(arena->allocator, ptr, new_size);
}


void wy_gc_free(wy_gc_arena* arena, void* ptr)
{
    wy_allocator_free(arena->allocator, ptr);
}


void wy_gc_track(wy_gc_arena* context, wy_object* gc_info)
{
    if (!context || !gc_info) { return; }
    if (!context->first) {
        context->first = gc_info;
        context->last = gc_info;
    } else {
        WY_ASSERT(context->last != WY_NULL);
        context->last->next = gc_info;
        context->last = gc_info;
    }
}



void wy_gc_collect_start_f(wy_context* parent, wy_gc_arena* self)
{
    WY_UNUSED(parent);
    if (parent == WY_NULL || self == WY_NULL) { return; }
    wy_object* gc_cur = self->first;
    while (gc_cur) {
        gc_cur->flags &= ~( (wy_uword) WY_GC_FLAG_MARKED );
        gc_cur = gc_cur->next;
    }
}


void wy_gc_object_visit(wy_state* state, wy_object* parent)
{
    wy_work_area wa;

    parent->flags |= WY_GC_FLAG_MARKED;

    if (wy_object_children_iter_start(state, parent, &wa) != WY_ERR_NONE) { return; }
    const wy_object* child = WY_NULL;

    while (wy_object_children_iter_next_f(state, parent, &wa, &child) == WY_ERR_NONE) {
        WY_ASSERT(child != WY_NULL);
        if ((child->flags & WY_GC_FLAG_MARKED) == 0 &&
            (child->flags & WY_GC_STATIC) == 0) {
            wy_gc_object_visit(state, (wy_object*) child);
        }
    }
}


void wy_gc_collect_finish_f(wy_context* parent, wy_gc_arena* self)
{
    if (parent == WY_NULL || self == WY_NULL) { return; }

    wy_object* first = WY_NULL;
    wy_object* last = WY_NULL;
    wy_object* gc_cur = self->first;

    while (gc_cur) {
        wy_object* next_gc = gc_cur->next;
        if ((gc_cur->flags & (WY_GC_FLAG_MARKED | WY_GC_STATIC)) != 0) {
            /* survivor: relink onto the new list */
            if (first == WY_NULL) { first = gc_cur; }
            else { last->next = gc_cur; }
            last = gc_cur;
            gc_cur->next = WY_NULL;
        } else {
            wy_object_finalize_f(parent, gc_cur);
            wy_gc_free(self, gc_cur);
        }
        gc_cur = next_gc;
    }

    self->first = first;
    self->last = last;
}
