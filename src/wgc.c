#include <wyrm/wgc.h>
#include <wyrm.h>


void wyrm_gc_init_f(wyrm_gc_arena* self, wyrm_allocator* allocator)
{
    self->first = WYRM_NULL;
    self->last = WYRM_NULL;
    self->allocator = allocator;
}


void wyrm_gc_finalize_f(wyrm_context* parent, wyrm_gc_arena* self)
{
    wyrm_object* next = self->first;
    for (wyrm_object* cur = next; cur; cur = next) {
        next = cur->next;
        wyrm_object_finalize_f(parent, cur);
        wyrm_gc_free(self, cur);
    }
}


void* wyrm_gc_alloc(wyrm_gc_arena* arena, wyrm_uword dsize)
{
    return wyrm_allocator_alloc(arena->allocator, dsize);
}


void* wyrm_gc_realloc(wyrm_gc_arena* arena, void* ptr, wyrm_uword new_size)
{
    return wyrm_allocator_realloc(arena->allocator, ptr, new_size);
}


void wyrm_gc_free(wyrm_gc_arena* arena, void* ptr)
{
    wyrm_allocator_free(arena->allocator, ptr);
}


void wyrm_gc_track(wyrm_gc_arena* context, wyrm_object* gc_info)
{
    if (!context || !gc_info) { return; }
    if (!context->first) {
        context->first = gc_info;
        context->last = gc_info;
    } else {
        WYRM_ASSERT(context->last != WYRM_NULL);
        context->last->next = gc_info;
        context->last = gc_info;
    }
}



void wyrm_gc_collect_start_f(wyrm_context* parent, wyrm_gc_arena* self)
{
    WYRM_UNUSED(parent);
    if (parent == WYRM_NULL || self == WYRM_NULL) { return; }
    wyrm_object* gc_cur = self->first;
    while (gc_cur) {
        gc_cur->flags &= ~( (wyrm_uword) WYRM_GC_FLAG_MARKED );
        gc_cur = gc_cur->next;
    }
}


void wyrm_gc_object_visit(wyrm_state* state, wyrm_object* parent)
{
    wyrm_work_area wa;

    parent->flags |= WYRM_GC_FLAG_MARKED;

    if (wyrm_object_children_iter_start(state, parent, &wa) != WYRM_ERR_NONE) { return; }
    const wyrm_object* child = WYRM_NULL;

    while (wyrm_object_children_iter_next_f(state, parent, &wa, &child) == WYRM_ERR_NONE) {
        WYRM_ASSERT(child != WYRM_NULL);
        if ((child->flags & WYRM_GC_FLAG_MARKED) == 0 &&
            (child->flags & WYRM_GC_STATIC) == 0) {
            wyrm_gc_object_visit(state, (wyrm_object*) child);
        }
    }
}


void wyrm_gc_collect_finish_f(wyrm_context* parent, wyrm_gc_arena* self)
{
    if (parent == WYRM_NULL || self == WYRM_NULL) { return; }

    wyrm_object* first = WYRM_NULL;
    wyrm_object* last = WYRM_NULL;
    wyrm_object* gc_cur = self->first;

    while (gc_cur) {
        wyrm_object* next_gc = gc_cur->next;
        if ((gc_cur->flags & (WYRM_GC_FLAG_MARKED | WYRM_GC_STATIC)) != 0) {
            /* survivor: relink onto the new list */
            if (first == WYRM_NULL) { first = gc_cur; }
            else { last->next = gc_cur; }
            last = gc_cur;
            gc_cur->next = WYRM_NULL;
        } else {
            wyrm_object_finalize_f(parent, gc_cur);
            wyrm_gc_free(self, gc_cur);
        }
        gc_cur = next_gc;
    }

    self->first = first;
    self->last = last;
}
