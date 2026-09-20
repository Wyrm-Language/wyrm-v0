#include <wyrm.h>
#include <wyrm/native.h>

static void init_common(wy_native* self, wy_symbol name, wy_u8 min_argc, wy_u8 max_argc)
{
    self->name = name;
    self->min_argc = min_argc;
    self->max_argc = max_argc;
}

wy_error wy_native_leaf_new(wy_context* context, wy_symbol name, wy_u8 min_argc, wy_u8 max_argc,
    wy_native_leaf_fn leaf_fn, wy_native** out)
{
    if (context == WY_NULL || leaf_fn == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_native* self = wy_context_gc_alloc(context, sizeof(wy_native));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    init_common(self, name, min_argc, max_argc);
    self->kind = WY_NATIVE_LEAF;
    self->fn.leaf = leaf_fn;

    wy_context_object_init_header_f(context, &self->object, &wy_native_type);
    *out = self;
    return WY_ERR_NONE;
}


wy_error wy_native_exec_new(wy_context* context, wy_symbol name, wy_u8 min_argc, wy_u8 max_argc,
    wy_exec_fn exec_fn, wy_native** out)
{
    if (context == WY_NULL || wy_exec_fn_is_empty(&exec_fn) || out == WY_NULL) { return WY_ERR_INVAL; }

    wy_native* self = wy_context_gc_alloc(context, sizeof(wy_native));
    if (self == WY_NULL) { return WY_ERR_NOMEM; }

    init_common(self, name, min_argc, max_argc);
    self->kind = WY_NATIVE_EXEC;
    self->fn.exec = exec_fn;

    wy_context_object_init_header_f(context, &self->object, &wy_native_type);
    *out = self;
    return WY_ERR_NONE;
}


const wy_object_type wy_native_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_NATIVE,

    /* no owned buffers and no wy_value references: name is a machine-lifetime symbol */
    .finalize = WY_NULL,
    .children_iter_start = WY_NULL,
    .children_iter_next = WY_NULL,
};
