#ifndef WYRM_MODULE_H_
#define WYRM_MODULE_H_

#include <wyrm/core.h>

WYRM_BEGIN_DECLS

extern const wyrm_object_type wy_module_type;

struct wy_module
{
    wyrm_object head;

    wy_uword global_count;
    wy_uword global_capacity;
    wy_value* globals;

    wy_u32* code;
    wy_uword code_capacity;
    wy_uword code_size;
};

#define WY_MODULE_GET_OBJ(self) (&((self)->head))

WYRM_INLINE wy_uword wy_module_get_global_capacity_f(wy_module* self) { return self->global_capacity; }
WYRM_INLINE wy_uword wy_module_get_global_count_f(wy_module* self) { return self->global_count; }
WYRM_INLINE wy_value* wy_module_get_global_f(wy_module* self, wy_uword idx) { return &self->globals[idx]; }
WYRM_INLINE wy_u32* wy_module_get_code_f(wy_module* self) { return self->code; }
WYRM_INLINE wy_uword wy_module_get_code_capacity_f(wy_module* self) { return self->code_capacity; }
WYRM_INLINE wy_uword wy_module_get_code_size(wy_module* self) { return self->code_size; }

void wy_module_init_static_f(wy_module* self);

wy_module* wy_module_new_f(wyrm_context* context);
wy_error wy_module_load(wyrm_context* context, wy_module* self, const wy_u8* dbuf, wy_uword dbuf_size);

wy_error wy_module_reserve_globals_f(wy_module* self, wy_allocator* allocator, wy_uword capacity);
wy_error wy_module_resize_globals_f(wy_module* self, wy_uword capacity);

wy_error wy_module_reserve_code_f(wyrm_context* context, wy_module* self, wy_uword capacity);
wy_error wy_module_code_push(wy_module* self, const wy_u32* code_buffer, wy_uword code_size, wy_uword* out_offset);


WYRM_END_DECLS

#endif
