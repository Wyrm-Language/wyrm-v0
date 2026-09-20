#include <wyrm.h>
#include <wyrm/iter.h>
#include <wyrm/list.h>
#include <wyrm/tuple.h>
#include <wyrm/dict.h>
#include <wyrm/pair.h>
#include <wyrm/string.h>
#include <wyrm/work_area.h>

static void finalize_f(wy_context* context, wy_object* object);
static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa);
static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child);

wy_error wy_iterator_new(wy_context* context, wy_value source, wy_iterator** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    if (source.type == WY_TYPE_TAG_ITER) {
        *out = (wy_iterator*) source.data.gc_object;
        return WY_ERR_NONE;
    }

    wy_iterator* self = wy_context_gc_alloc(context, sizeof(wy_iterator));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->source = source;
    self->current = source;
    self->state = 0;
    self->limit = 0;
    self->is_range = false;

    wy_context_object_init_header_f(context, &self->object, &wy_iterator_type);
    *out = self;
    return WY_ERR_NONE;
}

wy_error wy_iterator_new_range(wy_context* context, wy_word begin, wy_word end, wy_iterator** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_iterator* self = wy_context_gc_alloc(context, sizeof(wy_iterator));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->source = wy_value_nil();
    self->current = wy_value_word(begin);
    self->state = 0;
    self->limit = end;
    self->is_range = true;

    wy_context_object_init_header_f(context, &self->object, &wy_iterator_type);
    *out = self;
    return WY_ERR_NONE;
}

wy_error wy_iterator_next(wy_context* context, wy_iterator* self, wy_value* out)
{
    if (self->is_range) {
        wy_word v = self->current.data.word;
        if (v >= self->limit) { return WY_ERR_STOP_ITERATION; }
        *out = self->current;
        self->current = wy_value_word(v + 1);  /* v < limit, so no overflow */
        return WY_ERR_NONE;
    }

    wy_value src = self->source;

    if (src.type == WY_TYPE_TAG_LIST) {
        wy_list* list = (wy_list*) src.data.gc_object;
        if (self->state >= list->count) return WY_ERR_STOP_ITERATION;
        *out = list->items[self->state++];
        return WY_ERR_NONE;
    }
    if (src.type == WY_TYPE_TAG_TUPLE) {
        wy_tuple* tup = (wy_tuple*) src.data.gc_object;
        if (self->state >= tup->count) return WY_ERR_STOP_ITERATION;
        *out = tup->items[self->state++];
        return WY_ERR_NONE;
    }
    if (src.type == WY_TYPE_TAG_STR) {
        wy_string* s = src.data.str;
        if (self->state >= s->len) return WY_ERR_STOP_ITERATION;
        wy_u32 cp;
        wy_uword seq_len = wy_utf8_decode_f(s->str, s->len, self->state, &cp);
        wy_string* ch = WY_NULL;
        wy_error err = wy_string_new(context, s->str + self->state, seq_len, &ch);
        if (err != WY_ERR_NONE) return err;
        self->state += seq_len;
        *out = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) ch);
        return WY_ERR_NONE;
    }
    if (src.type == WY_TYPE_TAG_TABLE) {
        wy_dict* dict = (wy_dict*) src.data.gc_object;
        if (self->state >= dict->count) return WY_ERR_STOP_ITERATION;
        *out = dict->dense[self->state++].key;
        return WY_ERR_NONE;
    }
    if (src.type == WY_TYPE_TAG_PAIR) {
        if (self->current.type != WY_TYPE_TAG_PAIR) return WY_ERR_STOP_ITERATION;
        wy_pair* p = (wy_pair*) self->current.data.gc_object;
        *out = p->car;
        self->current = p->cdr;
        return WY_ERR_NONE;
    }

    return WY_ERR_STOP_ITERATION;
}

static void finalize_f(wy_context* context, wy_object* object)
{
    WY_UNUSED(context); WY_UNUSED(object);
}

static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = 0;
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_iterator* self = (wy_iterator*) object;
    wy_word step = wa->data[0].word;

    if (step == 0) {
        wa->data[0].word = 1;
        if (wy_value_is_gc_ref_f(self->source)) {
            *child = self->source.data.gc_object;
            return WY_ERR_NONE;
        }
    }
    if (step == 1) {
        wa->data[0].word = 2;
        if (wy_value_is_gc_ref_f(self->current)) {
            *child = self->current.data.gc_object;
            return WY_ERR_NONE;
        }
    }

    return WY_ERR_STOP_ITERATION;
}

const wy_object_type wy_iterator_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_ITER,

    .finalize = finalize_f,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
