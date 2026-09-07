#include <wyrm/platform/glib/mainloop.h>
#include <wyrm.h>

#include <string.h>
#include <glib.h>

// ----------------------------------------------------------------------------
// Source slot
// ----------------------------------------------------------------------------

struct glib_mainloop;

typedef enum {
    SOURCE_KIND_EMPTY = 0,
    SOURCE_KIND_TIMER,
    SOURCE_KIND_IDLE,
    SOURCE_KIND_WAKEABLE,
    SOURCE_KIND_IO,
} source_kind;

// Individually heap-allocated so its address is stable when the pointer
// array grows.  handle.uword == idx at all times.
typedef struct source_slot {
    struct glib_mainloop *loop;
    wy_uword            idx;
    wy_primitive        handle;
    guint                 glib_tag;
    wy_primitive        ud;
    source_kind           kind;
    union {
        struct { wy_source_cb        cb; } timer_idle;
        struct { wy_source_handle_cb cb; wy_handle fd; } io;
    } src;
} source_slot;

// ----------------------------------------------------------------------------
// Main loop state
// ----------------------------------------------------------------------------

typedef struct glib_mainloop {
    wy_main_loop  base;
    wy_allocator *allocator;
    GMainLoop      *gloop;
    GMainContext   *ctx;
    source_slot   **slots;
    wy_uword      capacity;
} glib_mainloop;

// ----------------------------------------------------------------------------
// Slot management
// ----------------------------------------------------------------------------

#define GLIB_INITIAL_CAPACITY ((wy_uword)8)

// Finds a free slot, lazily allocating or doubling the pointer array as needed.
// Returns WY_IDX_INVALID on allocation failure.
static wy_uword slot_alloc(glib_mainloop *loop) {
    for (wy_uword i = 0; i < loop->capacity; i++) {
        if (loop->slots[i]->kind == SOURCE_KIND_EMPTY)
            return i;
    }

    wy_uword new_cap = (loop->capacity == 0)
        ? GLIB_INITIAL_CAPACITY
        : loop->capacity * 2;

    source_slot **new_arr = wy_allocator_realloc(
        loop->allocator, loop->slots, new_cap * sizeof(source_slot *));
    if (!new_arr)
        return WY_IDX_INVALID;

    loop->slots = new_arr;

    wy_uword i;
    for (i = loop->capacity; i < new_cap; i++) {
        loop->slots[i] = wy_allocator_alloc(loop->allocator, sizeof(source_slot));
        if (!loop->slots[i])
            break;
        memset(loop->slots[i], 0, sizeof(source_slot));
        loop->slots[i]->idx          = i;
        loop->slots[i]->handle.uword = i;
    }

    if (i == loop->capacity)
        return WY_IDX_INVALID;

    wy_uword first_free = loop->capacity;
    loop->capacity = i;
    return first_free;
}

static void slot_destroy(gpointer ptr)
{
    source_slot* slot = (source_slot*) ptr;
    slot->kind = SOURCE_KIND_EMPTY;
    slot->glib_tag = 0;
}

// ----------------------------------------------------------------------------
// GLib callbacks
// ----------------------------------------------------------------------------

static gboolean timer_idle_dispatch(gpointer data) {
    source_slot   *slot = (source_slot *)data;
    WY_ASSERT(slot->kind != SOURCE_KIND_EMPTY);

    if (slot->kind == SOURCE_KIND_WAKEABLE) {
        GSource *gsrc = g_main_context_find_source_by_id(slot->loop->ctx, slot->glib_tag);
        if (!gsrc) {
            return G_SOURCE_REMOVE;
        }
        g_source_set_ready_time(gsrc, -1);
    }

    if (!slot->src.timer_idle.cb(slot->ud)) {
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

// wy_io_condition values are identical to GIOCondition, so the cast is safe.
static gboolean io_dispatch(GIOChannel *ch, GIOCondition cond, gpointer data) {
    WY_UNUSED(ch);
    source_slot   *slot = (source_slot *)data;

    WY_ASSERT(slot->kind == SOURCE_KIND_IO);

    bool cont = slot->src.io.cb(slot->src.io.fd, (wy_io_condition)cond, slot->ud);
    return cont ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static int glib_map_priority(wy_priority priority) {
    switch (priority) {
        case WY_PRIORITY_HIGH:
            return G_PRIORITY_HIGH;
        case WY_PRIORITY_DEFAULT:
            return G_PRIORITY_DEFAULT;
        case WY_PRIORITY_IDLE:
            return G_PRIORITY_DEFAULT_IDLE;
        default:
            return G_PRIORITY_DEFAULT;
    }
}

// ----------------------------------------------------------------------------
// vtable
// ----------------------------------------------------------------------------

static wy_error glib_add_fd(wy_main_loop* ref,
                               wy_primitive *out,
                               wy_handle fd,
                               wy_io_condition events,
                               wy_priority priority,
                               wy_source_handle_cb cb,
                               wy_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wy_uword idx = slot_alloc(loop);
    if (idx == WY_IDX_INVALID)
        return WY_ERR_NOMEM;
    source_slot *slot = loop->slots[idx];

    GIOChannel *ch  = g_io_channel_unix_new((int)fd);
    if (!ch) { return WY_ERR_NOMEM; }

    GSource    *src = g_io_create_watch(ch, (GIOCondition)events);
    g_io_channel_unref(ch);
    if (!src) {
        slot_destroy(slot);
        return WY_ERR_INVAL;
    }

    slot->loop        = loop;
    slot->ud          = ud;
    slot->kind        = SOURCE_KIND_IO;
    slot->src.io.cb   = cb;
    slot->src.io.fd   = fd;

    g_source_set_priority(src, glib_map_priority(priority));
    g_source_set_callback(src, G_SOURCE_FUNC(io_dispatch), slot, slot_destroy);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WY_ERR_NONE;
}

static wy_error glib_add_timer(wy_main_loop* ref,
                                  wy_primitive *out,
                                  uint32_t ms,
                                  wy_priority priority,
                                  wy_source_cb cb,
                                  wy_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wy_uword idx = slot_alloc(loop);
    if (idx == WY_IDX_INVALID)
        return WY_ERR_NOMEM;

    source_slot *slot       = loop->slots[idx];
    slot->loop              = loop;
    slot->ud                = ud;
    slot->kind              = SOURCE_KIND_TIMER;
    slot->src.timer_idle.cb = cb;

    GSource *src = g_timeout_source_new(ms);
    if (!src) {
        slot_destroy(slot);
        return WY_ERR_NOMEM;
    }
    g_source_set_priority(src, glib_map_priority(priority));
    g_source_set_callback(src, timer_idle_dispatch, slot, slot_destroy);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WY_ERR_NONE;
}

static wy_error glib_add_idle(wy_main_loop* ref,
                                 wy_primitive *out,
                                 wy_source_cb cb,
                                 wy_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wy_uword idx = slot_alloc(loop);
    if (idx == WY_IDX_INVALID)
        return WY_ERR_NOMEM;

    source_slot *slot       = loop->slots[idx];
    slot->loop              = loop;
    slot->ud                = ud;
    slot->kind              = SOURCE_KIND_IDLE;
    slot->src.timer_idle.cb = cb;

    GSource *src = g_idle_source_new();
    if (!src) {
        slot_destroy(slot);
        return WY_ERR_NOMEM;
    }
    g_source_set_priority(src, glib_map_priority(WY_PRIORITY_IDLE));
    g_source_set_callback(src, timer_idle_dispatch, slot, slot_destroy);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WY_ERR_NONE;
}

static wy_error glib_add_wakeable(wy_main_loop* ref,
                                     wy_primitive *out,
                                     wy_priority priority,
                                     wy_source_cb cb,
                                     wy_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wy_uword idx = slot_alloc(loop);
    if (idx == WY_IDX_INVALID)
        return WY_ERR_NOMEM;

    source_slot *slot       = loop->slots[idx];
    slot->loop              = loop;
    slot->ud                = ud;
    slot->kind              = SOURCE_KIND_WAKEABLE;
    slot->src.timer_idle.cb = cb;

    GSource *src = g_idle_source_new();
    if (!src) {
        slot_destroy(slot);
        return WY_ERR_NOMEM;
    }
    g_source_set_priority(src, glib_map_priority(priority));
    g_source_set_callback(src, timer_idle_dispatch, slot, slot_destroy);
    g_source_set_ready_time(src, -1);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WY_ERR_NONE;
}

static wy_error glib_trigger(wy_main_loop* ref,
                                wy_primitive src) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    wy_uword idx = src.uword;

    if (idx == WY_IDX_INVALID || idx >= loop->capacity) {
        return WY_ERR_INVAL;
    }

    source_slot *slot = loop->slots[idx];
    if (slot->kind != SOURCE_KIND_WAKEABLE) {
        return WY_ERR_INVAL;
    }

    GSource *gsrc = g_main_context_find_source_by_id(loop->ctx, slot->glib_tag);
    if (gsrc == WY_NULL) {
        return WY_ERR_INVAL;
    }

    g_source_set_ready_time(gsrc, 0);
    g_main_context_wakeup(loop->ctx);
    return WY_ERR_NONE;
}

static wy_error glib_remove(wy_main_loop* ref, wy_primitive src) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    wy_uword idx = src.uword;

    if (idx == WY_IDX_INVALID || idx >= loop->capacity)
        return WY_ERR_INVAL;

    source_slot *slot = loop->slots[idx];
    if (slot->kind == SOURCE_KIND_EMPTY)
        return WY_ERR_INVAL;

    GSource *gsrc = g_main_context_find_source_by_id(loop->ctx, slot->glib_tag);
    if (gsrc) {
        g_source_destroy(gsrc);
    } else {
        // indicates bad slot
        slot_destroy(slot);
    }

    return WY_ERR_NONE;
}

static wy_error glib_iterate(wy_main_loop* ref, bool may_block) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    g_main_context_iteration(loop->ctx, (gboolean)may_block);
    return WY_ERR_NONE;
}

static wy_error glib_run(wy_main_loop* ref) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    g_main_loop_run(loop->gloop);
    return WY_ERR_NONE;
}

static wy_error glib_quit(wy_main_loop* ref) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    g_main_loop_quit(loop->gloop);
    return WY_ERR_NONE;
}

// ----------------------------------------------------------------------------
// Vtable (exported so callers can use wy_main_loop_construct)
// ----------------------------------------------------------------------------

const wy_main_loop_vt wy_glib_mainloop_vt_ = {
    .add_fd       = glib_add_fd,
    .add_timer    = glib_add_timer,
    .add_idle     = glib_add_idle,
    .add_wakeable = glib_add_wakeable,
    .trigger      = glib_trigger,
    .remove       = glib_remove,
    .iterate      = glib_iterate,
    .run          = glib_run,
    .quit         = glib_quit
};

// ----------------------------------------------------------------------------
// Legacy named constructor (convenience wrapper)
// ----------------------------------------------------------------------------

wy_main_loop *wy_glib_mainloop_new(wy_allocator *alloc) {
    glib_mainloop *loop = wy_allocator_alloc(alloc, sizeof(glib_mainloop));
    if (!loop)
        return NULL;

    GMainContext* context = g_main_context_new();
    if (!context) { goto out_err_context_new; }

    GMainLoop* main_loop = g_main_loop_new(context, FALSE);
    if (!main_loop) { goto out_err_main_loop_new; }

    loop->base.vt   = &wy_glib_mainloop_vt_;
    loop->allocator = alloc;
    loop->ctx       = context;
    loop->gloop     = main_loop;
    loop->slots     = NULL;
    loop->capacity  = 0;

    return &loop->base;

out_err_main_loop_new:
    g_main_context_unref(context);

out_err_context_new:
    wy_allocator_free(alloc, loop);
    return NULL;
}

wy_error wy_glib_mainloop_destroy(wy_main_loop* ref) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    for (wy_uword i = 0; i < loop->capacity; i++) {
        source_slot *slot = loop->slots[i];
        if (slot->kind != SOURCE_KIND_EMPTY && slot->glib_tag != 0) {
            GSource *src = g_main_context_find_source_by_id(loop->ctx, slot->glib_tag);
            if (src)
                g_source_destroy(src);
        }
        wy_allocator_free(loop->allocator, slot);
    }

    wy_allocator_free(loop->allocator, loop->slots);
    g_main_loop_unref(loop->gloop);
    g_main_context_unref(loop->ctx);
    wy_allocator_free(loop->allocator, loop);
    return WY_ERR_NONE;
}

wy_uword wy_glib_mainloop_get_active_sources(wy_main_loop* ref)
{
    glib_mainloop *loop = (glib_mainloop *)ref;
    wy_uword count = 0;

    if (!loop) { return 0; }

    for (wy_uword i = 0; i < loop->capacity; i++) {
        source_slot *slot = loop->slots[i];
        if (slot->kind != SOURCE_KIND_EMPTY) { count++; }
    }

    return count;
}
