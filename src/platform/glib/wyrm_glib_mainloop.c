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
    wyrm_uword            idx;
    wyrm_primitive        handle;
    guint                 glib_tag;
    wyrm_primitive        ud;
    source_kind           kind;
    union {
        struct { wyrm_source_cb        cb; } timer_idle;
        struct { wyrm_source_handle_cb cb; wyrm_handle fd; } io;
    } src;
} source_slot;

// ----------------------------------------------------------------------------
// Main loop state
// ----------------------------------------------------------------------------

typedef struct glib_mainloop {
    wyrm_main_loop  base;
    wyrm_allocator *allocator;
    GMainLoop      *gloop;
    GMainContext   *ctx;
    source_slot   **slots;
    wyrm_uword      capacity;
} glib_mainloop;

// ----------------------------------------------------------------------------
// Slot management
// ----------------------------------------------------------------------------

#define GLIB_INITIAL_CAPACITY ((wyrm_uword)8)

// Finds a free slot, lazily allocating or doubling the pointer array as needed.
// Returns WYRM_IDX_INVALID on allocation failure.
static wyrm_uword slot_alloc(glib_mainloop *loop) {
    for (wyrm_uword i = 0; i < loop->capacity; i++) {
        if (loop->slots[i]->kind == SOURCE_KIND_EMPTY)
            return i;
    }

    wyrm_uword new_cap = (loop->capacity == 0)
        ? GLIB_INITIAL_CAPACITY
        : loop->capacity * 2;

    source_slot **new_arr = wyrm_allocator_realloc(
        loop->allocator, loop->slots, new_cap * sizeof(source_slot *));
    if (!new_arr)
        return WYRM_IDX_INVALID;

    loop->slots = new_arr;

    wyrm_uword i;
    for (i = loop->capacity; i < new_cap; i++) {
        loop->slots[i] = wyrm_allocator_alloc(loop->allocator, sizeof(source_slot));
        if (!loop->slots[i])
            break;
        memset(loop->slots[i], 0, sizeof(source_slot));
        loop->slots[i]->idx          = i;
        loop->slots[i]->handle.uword = i;
    }

    if (i == loop->capacity)
        return WYRM_IDX_INVALID;

    wyrm_uword first_free = loop->capacity;
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
    WYRM_ASSERT(slot->kind != SOURCE_KIND_EMPTY);

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

// wyrm_io_condition values are identical to GIOCondition, so the cast is safe.
static gboolean io_dispatch(GIOChannel *ch, GIOCondition cond, gpointer data) {
    WYRM_UNUSED(ch);
    source_slot   *slot = (source_slot *)data;

    WYRM_ASSERT(slot->kind == SOURCE_KIND_IO);

    bool cont = slot->src.io.cb(slot->src.io.fd, (wyrm_io_condition)cond, slot->ud);
    return cont ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static int glib_map_priority(wyrm_priority priority) {
    switch (priority) {
        case WYRM_PRIORITY_HIGH:
            return G_PRIORITY_HIGH;
        case WYRM_PRIORITY_DEFAULT:
            return G_PRIORITY_DEFAULT;
        case WYRM_PRIORITY_IDLE:
            return G_PRIORITY_DEFAULT_IDLE;
        default:
            return G_PRIORITY_DEFAULT;
    }
}

// ----------------------------------------------------------------------------
// vtable
// ----------------------------------------------------------------------------

static wyrm_error glib_add_fd(wyrm_main_loop* ref,
                               wyrm_primitive *out,
                               wyrm_handle fd,
                               wyrm_io_condition events,
                               wyrm_priority priority,
                               wyrm_source_handle_cb cb,
                               wyrm_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wyrm_uword idx = slot_alloc(loop);
    if (idx == WYRM_IDX_INVALID)
        return WYRM_ERR_NOMEM;
    source_slot *slot = loop->slots[idx];

    GIOChannel *ch  = g_io_channel_unix_new((int)fd);
    if (!ch) { return WYRM_ERR_NOMEM; }

    GSource    *src = g_io_create_watch(ch, (GIOCondition)events);
    g_io_channel_unref(ch);
    if (!src) {
        slot_destroy(slot);
        return WYRM_ERR_INVAL;
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
    return WYRM_ERR_NONE;
}

static wyrm_error glib_add_timer(wyrm_main_loop* ref,
                                  wyrm_primitive *out,
                                  uint32_t ms,
                                  wyrm_priority priority,
                                  wyrm_source_cb cb,
                                  wyrm_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wyrm_uword idx = slot_alloc(loop);
    if (idx == WYRM_IDX_INVALID)
        return WYRM_ERR_NOMEM;

    source_slot *slot       = loop->slots[idx];
    slot->loop              = loop;
    slot->ud                = ud;
    slot->kind              = SOURCE_KIND_TIMER;
    slot->src.timer_idle.cb = cb;

    GSource *src = g_timeout_source_new(ms);
    if (!src) {
        slot_destroy(slot);
        return WYRM_ERR_NOMEM;
    }
    g_source_set_priority(src, glib_map_priority(priority));
    g_source_set_callback(src, timer_idle_dispatch, slot, slot_destroy);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WYRM_ERR_NONE;
}

static wyrm_error glib_add_idle(wyrm_main_loop* ref,
                                 wyrm_primitive *out,
                                 wyrm_source_cb cb,
                                 wyrm_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wyrm_uword idx = slot_alloc(loop);
    if (idx == WYRM_IDX_INVALID)
        return WYRM_ERR_NOMEM;

    source_slot *slot       = loop->slots[idx];
    slot->loop              = loop;
    slot->ud                = ud;
    slot->kind              = SOURCE_KIND_IDLE;
    slot->src.timer_idle.cb = cb;

    GSource *src = g_idle_source_new();
    if (!src) {
        slot_destroy(slot);
        return WYRM_ERR_NOMEM;
    }
    g_source_set_priority(src, glib_map_priority(WYRM_PRIORITY_IDLE));
    g_source_set_callback(src, timer_idle_dispatch, slot, slot_destroy);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WYRM_ERR_NONE;
}

static wyrm_error glib_add_wakeable(wyrm_main_loop* ref,
                                     wyrm_primitive *out,
                                     wyrm_priority priority,
                                     wyrm_source_cb cb,
                                     wyrm_primitive ud) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    wyrm_uword idx = slot_alloc(loop);
    if (idx == WYRM_IDX_INVALID)
        return WYRM_ERR_NOMEM;

    source_slot *slot       = loop->slots[idx];
    slot->loop              = loop;
    slot->ud                = ud;
    slot->kind              = SOURCE_KIND_WAKEABLE;
    slot->src.timer_idle.cb = cb;

    GSource *src = g_idle_source_new();
    if (!src) {
        slot_destroy(slot);
        return WYRM_ERR_NOMEM;
    }
    g_source_set_priority(src, glib_map_priority(priority));
    g_source_set_callback(src, timer_idle_dispatch, slot, slot_destroy);
    g_source_set_ready_time(src, -1);
    slot->glib_tag = g_source_attach(src, loop->ctx);
    g_source_unref(src);

    *out = slot->handle;
    return WYRM_ERR_NONE;
}

static wyrm_error glib_trigger(wyrm_main_loop* ref,
                                wyrm_primitive src) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    wyrm_uword idx = src.uword;

    if (idx == WYRM_IDX_INVALID || idx >= loop->capacity) {
        return WYRM_ERR_INVAL;
    }

    source_slot *slot = loop->slots[idx];
    if (slot->kind != SOURCE_KIND_WAKEABLE) {
        return WYRM_ERR_INVAL;
    }

    GSource *gsrc = g_main_context_find_source_by_id(loop->ctx, slot->glib_tag);
    if (gsrc == WYRM_NULL) {
        return WYRM_ERR_INVAL;
    }

    g_source_set_ready_time(gsrc, 0);
    g_main_context_wakeup(loop->ctx);
    return WYRM_ERR_NONE;
}

static wyrm_error glib_remove(wyrm_main_loop* ref, wyrm_primitive src) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    wyrm_uword idx = src.uword;

    if (idx == WYRM_IDX_INVALID || idx >= loop->capacity)
        return WYRM_ERR_INVAL;

    source_slot *slot = loop->slots[idx];
    if (slot->kind == SOURCE_KIND_EMPTY)
        return WYRM_ERR_INVAL;

    GSource *gsrc = g_main_context_find_source_by_id(loop->ctx, slot->glib_tag);
    if (gsrc) {
        g_source_destroy(gsrc);
    } else {
        // indicates bad slot
        slot_destroy(slot);
    }

    return WYRM_ERR_NONE;
}

static wyrm_error glib_iterate(wyrm_main_loop* ref, bool may_block) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    g_main_context_iteration(loop->ctx, (gboolean)may_block);
    return WYRM_ERR_NONE;
}

static wyrm_error glib_run(wyrm_main_loop* ref) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    g_main_loop_run(loop->gloop);
    return WYRM_ERR_NONE;
}

static wyrm_error glib_quit(wyrm_main_loop* ref) {
    glib_mainloop *loop = (glib_mainloop *)ref;
    g_main_loop_quit(loop->gloop);
    return WYRM_ERR_NONE;
}

// ----------------------------------------------------------------------------
// Vtable (exported so callers can use wyrm_main_loop_construct)
// ----------------------------------------------------------------------------

const wyrm_main_loop_vt wyrm_glib_mainloop_vt_ = {
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

wyrm_main_loop *wyrm_glib_mainloop_new(wyrm_allocator *alloc) {
    glib_mainloop *loop = wyrm_allocator_alloc(alloc, sizeof(glib_mainloop));
    if (!loop)
        return NULL;

    GMainContext* context = g_main_context_new();
    if (!context) { goto out_err_context_new; }

    GMainLoop* main_loop = g_main_loop_new(context, FALSE);
    if (!main_loop) { goto out_err_main_loop_new; }

    loop->base.vt   = &wyrm_glib_mainloop_vt_;
    loop->allocator = alloc;
    loop->ctx       = context;
    loop->gloop     = main_loop;
    loop->slots     = NULL;
    loop->capacity  = 0;

    return &loop->base;

out_err_main_loop_new:
    g_main_context_unref(context);

out_err_context_new:
    wyrm_allocator_free(alloc, loop);
    return NULL;
}

wyrm_error wyrm_glib_mainloop_destroy(wyrm_main_loop* ref) {
    glib_mainloop *loop = (glib_mainloop *)ref;

    for (wyrm_uword i = 0; i < loop->capacity; i++) {
        source_slot *slot = loop->slots[i];
        if (slot->kind != SOURCE_KIND_EMPTY && slot->glib_tag != 0) {
            GSource *src = g_main_context_find_source_by_id(loop->ctx, slot->glib_tag);
            if (src)
                g_source_destroy(src);
        }
        wyrm_allocator_free(loop->allocator, slot);
    }

    wyrm_allocator_free(loop->allocator, loop->slots);
    g_main_loop_unref(loop->gloop);
    g_main_context_unref(loop->ctx);
    wyrm_allocator_free(loop->allocator, loop);
    return WYRM_ERR_NONE;
}

wyrm_uword wyrm_glib_mainloop_get_active_sources(wyrm_main_loop* ref)
{
    glib_mainloop *loop = (glib_mainloop *)ref;
    wyrm_uword count = 0;

    if (!loop) { return 0; }

    for (wyrm_uword i = 0; i < loop->capacity; i++) {
        source_slot *slot = loop->slots[i];
        if (slot->kind != SOURCE_KIND_EMPTY) { count++; }
    }

    return count;
}
