#include <wyrm/gc.h>
#include <wyrm.h>
#include <wyrm/util.h>
#include <wyrm/work_area.h>

void wy_gc_init_f(wy_gc_arena* self, wy_allocator* allocator)
{
    self->first = WY_NULL;
    self->last = WY_NULL;
    self->allocator = allocator;
    self->last_examined = 0;
    self->last_survivors = 0;
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


/** A gray worklist of objects marked but not yet scanned for children. */
typedef struct wy_gc_worklist
{
    wy_object** items;
    wy_uword count;
    wy_uword capacity;
} wy_gc_worklist;

static bool worklist_push_(wy_allocator* allocator, wy_gc_worklist* wl, wy_object* object)
{
    if (wl->count >= wl->capacity) {
        wy_uword new_capacity = wy_next_array_capacity(wl->capacity, 256);
        wy_object** grown = wy_allocator_realloc(allocator, wl->items, new_capacity * sizeof(wy_object*));
        if (grown == WY_NULL) { return false; }
        wl->items = grown;
        wl->capacity = new_capacity;
    }
    wl->items[wl->count++] = object;
    return true;
}

void wy_gc_object_visit(wy_context* context, wy_object* parent)
{
    if (context == WY_NULL || parent == WY_NULL) { return; }

    wy_gc_worklist wl = { .items = WY_NULL, .count = 0, .capacity = 0 };
    wy_allocator* allocator = context->arena.allocator;

    if ((parent->flags & WY_GC_FLAG_MARKED) == 0) {
        parent->flags |= WY_GC_FLAG_MARKED;
        if (!worklist_push_(allocator, &wl, parent)) {
            /* Single root, already marked; nothing pending to abandon. */
            return;
        }
    } else {
        return;
    }

    bool abandoned = false;
    while (wl.count > 0 && !abandoned) {
        wy_object* cur = wl.items[--wl.count];

        wy_work_area wa;
        if (wy_object_children_iter_start(context, cur, &wa) != WY_ERR_NONE) { continue; }

        const wy_object* child = WY_NULL;
        while (wy_object_children_iter_next_f(context, cur, &wa, &child) == WY_ERR_NONE) {
            if (child == WY_NULL) { continue; }
            wy_object* mutable_child = (wy_object*) child;
            if ((mutable_child->flags & (WY_GC_FLAG_MARKED | WY_GC_STATIC)) != 0) { continue; }

            mutable_child->flags |= WY_GC_FLAG_MARKED;
            if (!worklist_push_(allocator, &wl, mutable_child)) {
                context->gc_abandoned = true;
                abandoned = true;
                break;
            }
        }
    }

    wy_allocator_free(allocator, wl.items);
}


void wy_gc_collect_finish_f(wy_context* parent, wy_gc_arena* self)
{
    if (parent == WY_NULL || self == WY_NULL) { return; }

    wy_object* first = WY_NULL;
    wy_object* last = WY_NULL;
    wy_object* gc_cur = self->first;
    wy_uword examined = 0, survivors = 0;

    while (gc_cur) {
        wy_object* next_gc = gc_cur->next;
        examined++;
        if ((gc_cur->flags & (WY_GC_FLAG_MARKED | WY_GC_STATIC)) != 0) {
            survivors++;
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
    self->last_examined = examined;
    self->last_survivors = survivors;
}
