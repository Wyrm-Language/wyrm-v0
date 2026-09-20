#include <wyrm.h>
#include <wyrm/dict.h>
#include <wyrm/message.h>
#include <wyrm/sys/string.h>
#include <wyrm/work_area.h>

wy_error wy_message_new_f(wy_context* context, wy_symbol name, wy_module* owner, wy_message** out)
{
    if (context == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_message* self = wy_context_gc_alloc(context, sizeof(wy_message));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    self->name = name;
    self->owner = owner;
    self->overloads = WY_NULL;
    self->overload_count = 0;
    self->overload_capacity = 0;
    self->has_wildcard_or_ptype_arity1 = false;

    wy_context_object_init_header_f(context, (wy_object*) self, &wy_message_type);
    *out = self;
    return WY_ERR_NONE;
}

wy_error wy_message_add_overload_f(wy_context* context, wy_message* self, wy_u16 arity, const wy_value* types, wy_value body)
{
    if (self == WY_NULL || (arity > 0 && types == WY_NULL)) { return WY_ERR_INVAL; }
    if (arity > WY_OVERLOAD_MAX_ARITY) { return WY_ERR_RANGE; }

    if (self->overload_count >= self->overload_capacity) {
        wy_uword new_cap = (self->overload_capacity == 0) ? 4 : self->overload_capacity * 2;
        wy_overload* arr = wy_context_gc_realloc(context, self->overloads, sizeof(wy_overload) * new_cap);
        if (arr == WY_NULL) { return WY_ERR_NOMEM; }
        self->overloads = arr;
        self->overload_capacity = new_cap;
    }

    wy_overload* ov = &self->overloads[self->overload_count++];
    ov->arity = arity;
    for (wy_uword i = 0; i < arity; i++) { ov->types[i] = types[i]; }
    for (wy_uword i = arity; i < WY_OVERLOAD_MAX_ARITY; i++) { ov->types[i] = wy_value_nil(); }
    ov->body = body;

    if (arity == 1 && (ov->types[0].type == WY_TYPE_TAG_NIL || ov->types[0].type == WY_TYPE_TAG_PTYPE)) {
        self->has_wildcard_or_ptype_arity1 = true;
    }
    return WY_ERR_NONE;
}

wy_error wy_module_message_by_name_f(wy_context* context, wy_module* module, wy_symbol name, wy_message** out)
{
    if (context == WY_NULL || module == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    if (module->message_table == WY_NULL) {
        wy_error err = wy_dict_new(context, &module->message_table);
        if (err != WY_ERR_NONE) { return err; }
    }

    wy_value* existing = wy_dict_get(context, module->message_table, WY_TYPE_TAG_SYMBOL,
        (wy_primitive) { .symtab_entry = name });
    if (existing != WY_NULL) {
        *out = (wy_message*) existing->data.gc_object;
        return WY_ERR_NONE;
    }

    wy_message* msg = WY_NULL;
    wy_error err = wy_message_new_f(context, name, module, &msg);
    if (err != WY_ERR_NONE) { return err; }
    err = wy_dict_set(context, module->message_table, WY_TYPE_TAG_SYMBOL, (wy_primitive) { .symtab_entry = name },
        WY_TYPE_TAG_MESSAGE, (wy_primitive) { .gc_object = (wy_object*) msg });
    if (err != WY_ERR_NONE) { return err; }

    *out = msg;
    return WY_ERR_NONE;
}

wy_error wy_module_resolve_message_f(wy_context* context, wy_module* module, wy_uword message_idx, wy_message** out)
{
    if (context == WY_NULL || module == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (message_idx >= module->message_count) { return WY_ERR_RANGE; }

    wy_message_ref* ref = &module->messages[message_idx];
    if (ref->bound != WY_NULL) {
        *out = ref->bound;
        return WY_ERR_NONE;
    }

    if (ref->path_len > 1) {
        /* `mod::name`: needs a name -> module resolution epic 5's import
         * machinery supplies. Nothing in epic 4's corpus emits this. */
        return WY_ERR_NOSUPPORT;
    }

    wy_message* msg = WY_NULL;
    wy_error err = wy_module_message_by_name_f(context, module, module->symbols[ref->path[0]], &msg);
    if (err != WY_ERR_NONE) { return err; }

    ref->bound = msg;
    *out = msg;
    return WY_ERR_NONE;
}


static void finalize(wy_context* context, wy_object* object)
{
    wy_message* self = (wy_message*) object;
    wy_context_gc_free(context, self->overloads);
    self->overloads = WY_NULL;
}

/* Children: owner (if any), then every overload's types[0..arity) and body. */
static wy_error children_iter_start(wy_context* context, wy_object* object, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(object);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = -1; /* -1: owner not yet yielded; >= 0: overload index */
    wa->data[1].word = 0;  /* field within the current overload: 0..arity = types, arity = body */
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_message* self = (wy_message*) object;
    wy_word idx = wa->data[0].word;

    if (idx < 0) {
        wa->data[0].word = 0;
        if (self->owner != WY_NULL) {
            *child = (wy_object*) self->owner;
            return WY_ERR_NONE;
        }
        idx = 0;
    }

    while ((wy_uword) idx < self->overload_count) {
        const wy_overload* ov = &self->overloads[idx];
        wy_uword field = (wy_uword) wa->data[1].word;

        while (field < ov->arity) {
            wy_value v = ov->types[field];
            field++;
            if (wy_value_is_gc_ref_f(v)) {
                wa->data[1].word = (wy_word) field;
                *child = v.data.gc_object;
                return WY_ERR_NONE;
            }
        }
        if (field == ov->arity) {
            wa->data[1].word = (wy_word) (field + 1);
            if (wy_value_is_gc_ref_f(ov->body)) {
                *child = ov->body.data.gc_object;
                return WY_ERR_NONE;
            }
        }

        idx++;
        wa->data[0].word = idx;
        wa->data[1].word = 0;
    }

    return WY_ERR_STOP_ITERATION;
}

const wy_object_type wy_message_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_MESSAGE,

    .finalize = finalize,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
};
