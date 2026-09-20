#include <wyrm.h>
#include <wyrm/bytes.h>
#include <wyrm/work_area.h>

static void finalize_f(wy_context* context, wy_object* object);

wy_error wy_bytes_new(wy_context* context, const wy_u8* data, wy_uword len, wy_bytes** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (len > 0 && data == WY_NULL) { return WY_ERR_INVAL; }

    wy_bytes* self = wy_context_gc_alloc(context, sizeof(wy_bytes));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->len = 0;
    self->capacity = 0;
    self->data = WY_NULL;

    if (len > 0) {
        self->data = wy_context_gc_alloc(context, len);
        if (self->data == WY_NULL) {
            wy_context_gc_free(context, self);
            return WY_ERR_NOMEM;
        }
        wy_memcpy(self->data, data, len);
        self->capacity = len;
    }
    self->len = len;

    wy_context_object_init_header_f(context, &self->object, &wy_bytes_type);
    *out = self;
    return WY_ERR_NONE;
}


wy_error wy_bytes_reserve(wy_context* context, wy_bytes* self, wy_uword min_capacity)
{
    if (context == WY_NULL || self == WY_NULL) { return WY_ERR_INVAL; }
    if (min_capacity <= self->capacity) { return WY_ERR_NONE; }

    wy_u8* new_data = wy_context_gc_realloc(context, self->data, min_capacity);
    if (new_data == WY_NULL) { return WY_ERR_NOMEM; }

    self->data = new_data;
    self->capacity = min_capacity;
    return WY_ERR_NONE;
}


static void finalize_f(wy_context* context, wy_object* object)
{
    wy_bytes* self = (wy_bytes*) object;
    wy_context_gc_free(context, self->data);
    self->data = WY_NULL;
    self->len = 0;
    self->capacity = 0;
}


const wy_object_type wy_bytes_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_BYTES,

    .finalize = finalize_f,
    /* no wy_value references held: children_iter_start/next are unimplemented */
    .children_iter_start = WY_NULL,
    .children_iter_next = WY_NULL,
};
