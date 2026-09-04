#include <wyrm/module.h>
#include <wyrm/allocator.h>
#include <wyrm/context.h>
#include <wyrm/sys/string.h>
#include <wyrm/bson.h>
#include "wyrm/object.h"

#include <stdio.h>

static wy_error module_handle_code_section_f(wyrm_context* context, wy_module* self, const wy_u8* section_ptr, wy_u8 section_type, wy_uword section_size);


void wy_module_init_static_f(wy_module* self)
{
    wyrm_object_init_header_s(&self->head, &wy_module_type);
    self->global_count = 0;
    self->global_capacity = 0;
    self->globals = NULL;
}

wy_module* wy_module_new_f(wyrm_context* context)
{
    wy_module* self = (wy_module*) wyrm_context_gc_alloc(context, sizeof(wy_module));
    if (!self) { return WYRM_NULL; }

    wyrm_context_push_gc(context, WY_MODULE_GET_OBJ(self));
    return self;
}

wy_error wy_module_load(wyrm_context* context, wy_module* self, const wy_u8* dbuf, wy_uword dbuf_size)
{
    WYRM_UNUSED(self);
    WYRM_UNUSED(context);

    if (dbuf_size < 8) { return WYRM_ERR_INVAL; }
    if (dbuf[0] != (wy_u8) 'W' ||
        dbuf[1] != (wy_u8) 'Y' ||
        dbuf[2] != (wy_u8) 'C' ||
        dbuf[3] != (wy_u8) 0x02) {
        return WYRM_ERR_INVAL;
    }

    /* self *must be* a from scratch / uninitialized module */
    if (self->code_size > 0 || self->global_count > 0) { return WYRM_ERR_INVAL; }

    const wy_u8* section_ptr;
    wy_u8 section_type;
    wy_uword section_size;
    const char* section_name;

    wy_bson_doc_reader doc_reader;
    wy_bson_doc_reader_start(&doc_reader, (const wy_u8*) dbuf + 4, dbuf_size - 4);

    // Header
    wy_error last_error = WYRM_ERR_NONE;
    while (last_error == WYRM_ERR_NONE) {
        last_error = wy_bson_doc_reader_get(&doc_reader,
            &section_type, &section_name,
            &section_ptr, &section_size);
        if (last_error != WYRM_ERR_NONE) { break; }

        if (wyrm_strcmp_f(section_name, "header") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "statics") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "slot_defaults") == 0) {

        }
        else if (wyrm_strcmp_f( section_name, "symbols") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "functions") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "classes") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "messages") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "code") == 0) {
            last_error = module_handle_code_section_f(context, self, section_ptr, section_type, section_size);
        }
        else if (wyrm_strcmp_f(section_name, "debug") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "exports") == 0) {

        }
        else if (wyrm_strcmp_f(section_name, "free") == 0) {

        }
    }

    if (last_error == WYRM_ERR_STOP_ITERATION) {
        last_error = WYRM_ERR_NONE;
    }


    return last_error;
}

static wy_error module_handle_code_section_f(wy_context* context, wy_module* self, const wy_u8* section_ptr, wy_u8 section_type, wy_uword section_size)
{
    if (section_type != WY_BSON_TAG_BINARY) { return WYRM_ERR_INVAL; }
    wy_error last_error = WYRM_ERR_NONE;
    const wy_u8* code_buffer;
    wy_u8 code_subtype;
    wy_uword code_size;

    last_error = wy_bson_get_binary_f(section_ptr, section_size, &code_buffer, &code_subtype, &code_size);
    if (last_error != WYRM_ERR_NONE) { return last_error; }

    last_error = wy_module_reserve_code_f(context, self, code_size);
    if (last_error != WYRM_ERR_NONE) { return last_error; }

    wy_uword drop;
    last_error = wy_module_code_push(self, (const wy_u32*) code_buffer, code_size, &drop);
    return last_error;
}

wy_error wy_module_init_reserve_f(wy_module* self, wy_allocator* allocator, wy_uword capacity)
{
    if (self->global_capacity >= capacity) { return WYRM_ERR_NONE; }
    if (capacity == 0 || allocator == WYRM_NULL) { return WYRM_ERR_INVAL; }

    wyrm_value* new_globals = (wyrm_value*) wyrm_allocator_realloc(allocator, self->globals, sizeof(wyrm_value) * capacity);
    if (new_globals == NULL) { return WYRM_ERR_NOMEM; }

    self->global_capacity = capacity;
    self->globals = new_globals;
    return WYRM_ERR_NONE;
}

wy_error wy_module_resize_globals_f(wy_module* self, wy_uword capacity)
{
    if (capacity > self->global_capacity) { return WYRM_ERR_NOMEM; }
    for (wy_uword idx = self->global_count; idx < capacity; idx++) {
        self->globals[idx] = wyrm_value_Unset();
    }
    self->global_count = capacity;
    return WYRM_ERR_NONE;
}

wy_error wy_module_reserve_code_f(wyrm_context* context, wy_module* self, wy_uword capacity)
{
    if (capacity <= self->code_capacity) { return WYRM_ERR_NONE; }
    if (capacity == 0 || context == WYRM_NULL) { return WYRM_ERR_INVAL; }

    wy_u32* new_code = (wy_u32*) wyrm_context_gc_realloc(context, self->code, sizeof(wy_u32) * capacity);
    if (new_code == NULL) { return WYRM_ERR_NOMEM; }

    self->code_capacity = capacity;
    self->code = new_code;
    return WYRM_ERR_NONE;
}

wy_error wy_module_code_push(wy_module* self, const wy_u32* code_buffer, wy_uword code_size, wy_uword* out_offset)
{
    wy_uword new_size = self->code_size + code_size;
    wy_uword orig = self->code_size;
    if (new_size <= self->code_capacity) {
        wyrm_memcpy(self->code + self->code_size, code_buffer, sizeof(wy_u32) * code_size);
        self->code_size = new_size;
        if (out_offset != WYRM_NULL) { *out_offset = orig; }
        return WYRM_ERR_NONE;
    }
    return WYRM_ERR_NOMEM;
}

void finalize(wyrm_context* context, wyrm_object* self_s)
{
    wy_module* self = (wy_module*) self_s;
    wyrm_context_gc_free(context, self->globals);
    self->globals = NULL;
}


wyrm_error children_iter_start(wyrm_state* state, wyrm_object* self, wyrm_work_area* wa)
{
    WYRM_UNUSED(state); WYRM_UNUSED(self);
    wyrm_memset(wa, 0, sizeof(wyrm_work_area));
    wa->data[0].uword = 0;
    wa->data[1].uword = 0;
    return WYRM_ERR_NONE;
}

wyrm_error children_iter_next(wyrm_state* state, wyrm_object* object, wyrm_work_area* wa, const wyrm_object** child)
{
    WYRM_UNUSED(state);
    wy_module* self = (wy_module*) object;

    for (wy_uword idx = wa->data[0].word; idx < self->global_count; idx++) {
        if (wy_type_is_object(self->globals[idx].type)) {
            wa->data[0].uword = idx;
            *child = self->globals[idx].data.gc_object;
            return WYRM_ERR_NONE;
        }
    }

    return WYRM_ERR_STOP_ITERATION;
}


const wyrm_object_type wy_module_type = {
    .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WYRM_TYPE_TAG_CLASS,

    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
    .finalize = finalize,
};
