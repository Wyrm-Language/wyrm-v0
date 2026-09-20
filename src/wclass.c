#include <wyrm.h>
#include <wyrm/allocator.h>
#include <wyrm/function.h>
#include <wyrm/message.h>
#include <wyrm/sys/string.h>
#include <wyrm/work_area.h>

wy_error wy_class_new(wy_context* context, wy_class** out)
{
    wy_class* cls = wy_context_gc_alloc(context, sizeof(wy_class));
    if (!cls) { return WY_ERR_NOMEM; }

    cls->name = WY_NULL;
    cls->super = WY_NULL;
    cls->module = WY_NULL;
    cls->slot_count = 0;
    cls->depth = 0;
    cls->flags = 0;
    cls->msg_count = 0;
    cls->slots = WY_NULL;
    for (wy_uword i = 0; i < WY_CLASS_MSG_MAP_SIZE; i++) {
        cls->msg_map[i].msg = WY_NULL;
        cls->msg_map[i].body = wy_value_unset();
    }
    cls->statics = (wy_slot_dict) WY_SLOT_DICT_INITIALIZER;
    cls->init = wy_value_unset();

    wy_context_object_init_header_f(context, (wy_object*) cls, &wy_type_class);
    *out = cls;
    return WY_ERR_NONE;
}

/** Realise a function proto index into a zero-capture FUNCTION value, or Unset for -1. */
static wy_error realise_fn_or_unset_(wy_context* context, wy_module* module, wy_i32 fn_idx, wy_value* out)
{
    if (fn_idx < 0) {
        *out = wy_value_unset();
        return WY_ERR_NONE;
    }
    wy_function* fn = WY_NULL;
    wy_error err = wy_function_new(context, module, &module->functions[fn_idx], WY_NULL, 0, &fn);
    if (err != WY_ERR_NONE) { return err; }
    *out = wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn);
    return WY_ERR_NONE;
}

wy_error wy_class_realise_f(wy_context* context, wy_module* module, wy_uword class_idx, wy_class** out)
{
    if (context == WY_NULL || module == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (class_idx >= module->class_count) { return WY_ERR_RANGE; }

    if (module->classes == WY_NULL) {
        module->classes = wy_context_gc_alloc(context, sizeof(wy_class*) * module->class_count);
        if (module->classes == WY_NULL) { return WY_ERR_NOMEM; }
        for (wy_uword i = 0; i < module->class_count; i++) { module->classes[i] = WY_NULL; }
    }

    if (module->classes[class_idx] != WY_NULL) {
        *out = module->classes[class_idx];
        return WY_ERR_NONE;
    }

    const wy_class_proto* proto = &module->class_protos[class_idx];

    wy_class* super = WY_NULL;
    if (proto->super_slot >= 0) {
        wy_value super_v = module->globals[proto->super_slot];
        if (super_v.type != WY_TYPE_TAG_CLASS) { return WY_ERR_BAD_TYPE; }
        super = (wy_class*) super_v.data.gc_object;
    }

    wy_class* cls = WY_NULL;
    wy_error err = wy_class_new(context, &cls);
    if (err != WY_ERR_NONE) { return err; }

    cls->name = proto->name;
    cls->super = super;
    cls->module = module;
    cls->depth = super ? (wy_u16) (super->depth + 1) : 0;

    wy_uword base_count = super ? super->slot_count : 0;
    wy_uword total_slots = base_count + proto->nslots;
    if (total_slots > 0) {
        cls->slots = wy_context_gc_alloc(context, sizeof(wy_class_slot) * total_slots);
        if (cls->slots == WY_NULL) { return WY_ERR_NOMEM; }
    }
    for (wy_uword i = 0; i < base_count; i++) {
        cls->slots[i] = super->slots[i];
    }
    for (wy_uword i = 0; i < proto->nslots; i++) {
        const wy_slot_proto* sp = &proto->slots[i];
        wy_class_slot* out_slot = &cls->slots[base_count + i];
        out_slot->name = sp->name;
        out_slot->default_value = (sp->default_static >= 0)
            ? module->statics[sp->default_static]
            : wy_value_unset();
        err = realise_fn_or_unset_(context, module, sp->getter_fn, &out_slot->getter);
        if (err != WY_ERR_NONE) { return err; }
        err = realise_fn_or_unset_(context, module, sp->setter_fn, &out_slot->setter);
        if (err != WY_ERR_NONE) { return err; }
    }
    cls->slot_count = (wy_u16) total_slots;

    cls->msg_count = proto->nmsgs;
    for (wy_uword i = 0; i < proto->nmsgs; i++) {
        err = realise_fn_or_unset_(context, module, (wy_i32) proto->msgs[i].fn, &cls->msg_map[i].body);
        if (err != WY_ERR_NONE) { return err; }

        /* design_c_vm.md §7: "class realisation registers each map entry
         * as overload ((cls), FUNCTION) on the module's message identity" -
         * a single-arity overload constrained to this exact class. */
        wy_message* msg = WY_NULL;
        err = wy_module_message_by_name_f(context, module, proto->msgs[i].name, &msg);
        if (err != WY_ERR_NONE) { return err; }
        wy_value cls_type = wy_value_object(WY_TYPE_TAG_CLASS, (wy_object*) cls);
        err = wy_message_add_overload_f(context, msg, 1, &cls_type, cls->msg_map[i].body);
        if (err != WY_ERR_NONE) { return err; }
        cls->msg_map[i].msg = msg;
    }

    if (proto->nstatics > 0) {
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        err = wy_slot_dict_expand_f(&cls->statics, allocator, proto->nstatics * 2);
        if (err != WY_ERR_NONE) { return err; }
        for (wy_uword i = 0; i < proto->nstatics; i++) {
            err = wy_slot_dict_add_entry(&cls->statics, proto->statics[i].name, proto->statics[i].global);
            if (err != WY_ERR_NONE) { return err; }
        }
    }

    err = realise_fn_or_unset_(context, module, proto->init_fn, &cls->init);
    if (err != WY_ERR_NONE) { return err; }

    module->classes[class_idx] = cls;
    *out = cls;
    return WY_ERR_NONE;
}

wy_uword wy_class_distance_f(wy_class* cls, wy_class* target)
{
    wy_uword dist = 0;
    while (cls) {
        if (cls == target) { return dist; }
        cls = cls->super;
        dist++;
    }
    return WY_WILDCARD_DISTANCE;
}

wy_value wy_class_find_init_f(wy_class* cls)
{
    for (wy_class* c = cls; c != WY_NULL; c = c->super) {
        if (!wy_value_is_unset(c->init)) { return c->init; }
    }
    return wy_value_unset();
}

wy_class_slot* wy_class_find_slot_f(wy_class* cls, wy_symbol name, wy_uword* index_out)
{
    for (wy_class* c = cls; c != WY_NULL; c = c->super) {
        for (wy_uword i = 0; i < c->slot_count; i++) {
            /* Base-first layout (wyc-format.md §8.6) is shared across the
             * hierarchy, so an ancestor's own slots array already holds the
             * same base-first indices this class inherited. */
            if (c->slots[i].name == name) {
                if (index_out != WY_NULL) { *index_out = i; }
                return &c->slots[i];
            }
        }
    }
    return WY_NULL;
}


static void finalize(wy_context* context, wy_object* self)
{
    wy_class* cls = (wy_class*) self;
    wy_context_gc_free(context, cls->slots);
    cls->slots = WY_NULL;
    if (cls->statics.entry_table != WY_NULL) {
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        wy_slot_finalize_f(&cls->statics, allocator);
    }
}


/*
 * Children: module (if any), super, every slot's default/getter/setter,
 * every msg_map body, and init - in that fixed order, indexed by
 * wa->data[0].word.
 */
enum {
    CI_MODULE = 0,
    CI_SUPER,
    CI_SLOTS,
    CI_MSGS,
    CI_INIT,
    CI_DONE,
};

static wy_error children_iter_start(wy_context* context, wy_object* self, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(self);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].word = CI_MODULE;
    wa->data[1].word = 0;
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* self, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);
    wy_class* cls = (wy_class*) self;

    if (wa->data[0].word == CI_MODULE) {
        wa->data[0].word = CI_SUPER;
        if (cls->module != WY_NULL) {
            *child = (wy_object*) cls->module;
            return WY_ERR_NONE;
        }
    }

    if (wa->data[0].word == CI_SUPER) {
        wa->data[0].word = CI_SLOTS;
        wa->data[1].word = 0;
        if (cls->super != WY_NULL) {
            *child = (wy_object*) cls->super;
            return WY_ERR_NONE;
        }
    }

    if (wa->data[0].word == CI_SLOTS) {
        while ((wy_uword) wa->data[1].word < cls->slot_count) {
            wy_uword i = (wy_uword) wa->data[1].word;
            const wy_class_slot* slot = &cls->slots[i];
            wy_value candidates[3] = { slot->default_value, slot->getter, slot->setter };
            wy_uword field = (wy_uword) wa->data[2].word;
            while (field < 3) {
                wy_value v = candidates[field];
                field++;
                if (wy_value_is_gc_ref_f(v)) {
                    wa->data[2].word = (wy_word) field;
                    *child = v.data.gc_object;
                    return WY_ERR_NONE;
                }
            }
            wa->data[2].word = 0;
            wa->data[1].word++;
        }
        wa->data[0].word = CI_MSGS;
        wa->data[1].word = 0;
    }

    if (wa->data[0].word == CI_MSGS) {
        while ((wy_uword) wa->data[1].word < cls->msg_count) {
            wy_uword i = (wy_uword) wa->data[1].word;
            wa->data[1].word++;
            if (wy_value_is_gc_ref_f(cls->msg_map[i].body)) {
                *child = cls->msg_map[i].body.data.gc_object;
                return WY_ERR_NONE;
            }
        }
        wa->data[0].word = CI_INIT;
    }

    if (wa->data[0].word == CI_INIT) {
        wa->data[0].word = CI_DONE;
        if (wy_value_is_gc_ref_f(cls->init)) {
            *child = cls->init.data.gc_object;
            return WY_ERR_NONE;
        }
    }

    return WY_ERR_STOP_ITERATION;
}

const wy_object_type wy_type_class = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_CLASS,

    .finalize = finalize,
    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
 };
