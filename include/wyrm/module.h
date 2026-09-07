#ifndef WYRM_MODULE_H_
#define WYRM_MODULE_H_

#include <wyrm/core.h>
#include <wyrm/mem_info.h>

WYRM_BEGIN_DECLS

extern const wyrm_object_type wy_module_type;

struct wy_module
{
    wyrm_object head;

    wy_mem_info global_memory;
    wy_uword global_count;

    wy_mem_info code_memory;
    wy_uword code_count;
};

#define WY_MODULE_GET_OBJ(self) (&((self)->head))
#define WY_MODULE_GET_CODE_PTR(self) (WY_MEM_INFO_BEGIN_PTR(wy_u32, &(self)->code_memory))
#define WY_MODULE_GET_GLOBALS_PTR(self) (WY_MEM_INFO_BEGIN_PTR(wy_value, &(self)->global_memory))

WYRM_INLINE wy_uword wy_module_get_global_count_f(wy_module* self) { return self->global_count; }
WYRM_INLINE wy_value* wy_module_get_global_f(wy_module* self, wy_uword idx) { return &WY_MODULE_GET_GLOBALS_PTR(self)[idx]; }
WYRM_INLINE wy_u32* wy_module_get_code_f(wy_module* self) { return WY_MODULE_GET_CODE_PTR(self); }
WYRM_INLINE wy_uword wy_module_get_code_capacity_f(wy_module* self) { return WY_MEM_INFO_COUNT(wy_u32, &self->code_memory); }
WYRM_INLINE wy_uword wy_module_get_code_len(wy_module* self) { return self->code_count; }

void wy_module_init_static_f(wy_module* self);

wy_module* wy_module_new_f(wyrm_context* context);
wy_error wy_module_load(wyrm_context* context, wy_module* self, const wy_u8* dbuf, wy_uword dbuf_size);

wy_error wy_module_reserve_code_f(wyrm_context* context, wy_module* self, wy_uword capacity);
wy_error wy_module_code_push(wy_module* self, const wy_u8* code_buffer, wy_uword len, wy_uword* out_offset);


WYRM_END_DECLS

#endif
