#include <wyrm/module.h>
#include <wyrm/allocator.h>
#include <wyrm/context.h>
#include <wyrm/sys/string.h>
#include <wyrm/bson.h>
#include <wyrm/object.h>
#include <wyrm/value.h>
#include <wyrm/work_area.h>


static wy_error module_handle_code_section_f(wy_context* context, wy_module* self, const wy_u8* section_ptr, wy_u8 section_type, wy_uword section_size);


void wy_module_init_static_f(wy_module* self)
{
    wy_object_init_header_s(&self->head, &wy_module_type);
    self->global_count = 0;
    self->code_count = 0;

    wy_mem_info_init_empty_s(&self->code_memory);
    wy_mem_info_init_empty_s(&self->global_memory);
}

wy_module* wy_module_new_f(wy_context* context)
{
    wy_module* self = (wy_module*) wy_context_gc_alloc(context, sizeof(wy_module));
    if (!self) { return WY_NULL; }

    wy_module_init_static_f(self);
    wy_context_push_gc(context, WY_MODULE_GET_OBJ(self));
    return self;
}

wy_error wy_module_load(wy_context* context, wy_module* self, const wy_u8* dbuf, wy_uword dbuf_size)
{
    if (dbuf_size < 8) { return WY_ERR_INVAL; }
    if (dbuf[0] != (wy_u8) 'W' ||
        dbuf[1] != (wy_u8) 'Y' ||
        dbuf[2] != (wy_u8) 'C' ||
        dbuf[3] != (wy_u8) 0x02) {
        return WY_ERR_INVAL;
    }

    /* self *must be* a from scratch / uninitialized module */
    if (self->code_count > 0 || self->global_count > 0) { return WY_ERR_INVAL; }

    const wy_u8* section_ptr;
    wy_u8 section_type;
    wy_uword section_size;
    const char* section_name;

    wy_bson_doc_reader doc_reader;
    wy_bson_doc_reader_start(&doc_reader, (const wy_u8*) dbuf + 4, dbuf_size - 4);

    // Header
    wy_error last_error = WY_ERR_NONE;
    while (last_error == WY_ERR_NONE) {
        last_error = wy_bson_doc_reader_get(&doc_reader,
            &section_type, &section_name,
            &section_ptr, &section_size);
        if (last_error != WY_ERR_NONE) { break; }

        if (wy_strcmp_f(section_name, "header") == 0) {

        }
        else if (wy_strcmp_f(section_name, "statics") == 0) {

        }
        else if (wy_strcmp_f(section_name, "slot_defaults") == 0) {

        }
        else if (wy_strcmp_f( section_name, "symbols") == 0) {

        }
        else if (wy_strcmp_f(section_name, "functions") == 0) {

        }
        else if (wy_strcmp_f(section_name, "classes") == 0) {

        }
        else if (wy_strcmp_f(section_name, "messages") == 0) {

        }
        else if (wy_strcmp_f(section_name, "code") == 0) {
            last_error = module_handle_code_section_f(context, self, section_ptr, section_type, section_size);
        }
        else if (wy_strcmp_f(section_name, "debug") == 0) {

        }
        else if (wy_strcmp_f(section_name, "exports") == 0) {

        }
        else if (wy_strcmp_f(section_name, "free") == 0) {

        }
    }

    if (last_error == WY_ERR_STOP_ITERATION) {
        last_error = WY_ERR_NONE;
    }


    return last_error;
}

static wy_error module_handle_code_section_f(wy_context* context, wy_module* self, const wy_u8* section_ptr, wy_u8 section_type, wy_uword section_size)
{
    if (section_type != WY_BSON_TAG_BINARY) { return WY_ERR_INVAL; }
    if (self->code_count > 0) { /* Unexpected code section! */ return WY_ERR_INVAL; }
    wy_error last_error = WY_ERR_NONE;
    const wy_u8* code_buffer;
    wy_u8 code_subtype;
    wy_uword code_sz;

    last_error = wy_bson_get_binary_f(section_ptr, section_size, &code_buffer, &code_subtype, &code_sz);
    if (last_error != WY_ERR_NONE) { return last_error; }

    if ((code_sz % 4) != 0) { return WY_ERR_INVAL; }
    wy_uword code_len = code_sz / 4;

    last_error = wy_module_reserve_code_f(context, self, code_len);
    if (last_error != WY_ERR_NONE) { return last_error; }

    wy_uword drop;
    last_error = wy_module_code_push(self, code_buffer, code_len, &drop);
    return last_error;
}

wy_error wy_module_reserve_code_f(wy_context* context, wy_module* self, wy_uword len)
{
    if (WY_MEM_INFO_COUNT(wy_u32, &self->code_memory) >= len) { return WY_ERR_NONE; }
    if (len == 0) { return WY_ERR_INVAL; }

    wy_error last_error = WY_CONTEXT_MEM_INFO_RESERVE_COUNT(context, &self->code_memory, len, wy_u32);
    if (last_error != WY_ERR_NONE) { return last_error; }

    return WY_ERR_NONE;
}

/**
 * Push code to the module buffer.
 *
 * Append the code to the module buffer. This will *not* attempt to reallocate
 * if the buffer is insufficiently sized. Generally, resizing the module
 * code buffer requires invalidating and verifying any outstanding code
 * pointers.
 *
 * @param self Module target
 * @param code_buffer Binary buffer (may be unaligned)
 * @param len Total u32 code points to push
 * @param out_offset Pointer to the beginning of pushed data
 * @return WY_ERR_NONE on success otherwise appropriate error
 */
wy_error wy_module_code_push(wy_module* self, const wy_u8* code_buffer, wy_uword len, wy_uword* out_offset)
{
    wy_uword new_len = self->code_count + len;
    wy_uword orig = self->code_count;
    if (new_len <= WY_MEM_INFO_COUNT(wy_u32, &self->code_memory)) {
        wy_memcpy(WY_MEM_INFO_BEGIN_PTR(wy_u32, &self->code_memory) + orig,
            code_buffer,
            sizeof(wy_u32) * len);
        self->code_count = new_len;
        if (out_offset != WY_NULL) { *out_offset = orig; }
        return WY_ERR_NONE;
    }
    return WY_ERR_NOMEM;
}

static void finalize(wy_context* context, wy_object* self_s)
{
    wy_module* self = (wy_module*) self_s;

    wy_context_mem_release_f(context, &self->code_memory); self->code_count = 0;
    wy_context_mem_release_f(context, &self->global_memory); self->global_count = 0;
}


static wy_error children_iter_start(wy_context* context, wy_object* self, wy_work_area* wa)
{
    WY_UNUSED(context); WY_UNUSED(self);
    wy_memset(wa, 0, sizeof(wy_work_area));
    wa->data[0].uword = 0;
    wa->data[1].uword = 0;
    return WY_ERR_NONE;
}

static wy_error children_iter_next(wy_context* context, wy_object* object, wy_work_area* wa, const wy_object** child)
{
    WY_UNUSED(context);

    wy_module* self = (wy_module*) object;
    wy_value* globals = WY_MEM_INFO_BEGIN_PTR(wy_value, &self->global_memory);

    for (wy_uword cur = wa->data[0].uword; cur < self->global_count; cur = wa->data[0].uword) {
        wa->data[0].uword++;

        if (wy_value_is_gc_ref_f(globals[cur])) {
            *child = globals[cur].data.gc_object;
            return WY_ERR_NONE;
        }
    }

    return WY_ERR_STOP_ITERATION;
}


const wy_object_type wy_module_type = {
    .object = WY_OBJECT_TYPE_OBJECT_INIT,
    .gc_type = WY_TYPE_TAG_MODULE,

    .children_iter_start = children_iter_start,
    .children_iter_next = children_iter_next,
    .finalize = finalize,
};
