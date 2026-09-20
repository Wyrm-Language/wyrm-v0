#ifndef WYRM_MACHINE_H_
#define WYRM_MACHINE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>
#include <wyrm/fwd.h>
#include <wyrm/symtab_entry.h>
#include <wyrm/symtab.h>

WY_BEGIN_DECLS

struct wy_machine
{
    wy_allocator* allocator;
    wy_context* context;

    wy_symtab symtab;
};


wy_error wy_machine_init_s(wy_machine* self, wy_allocator* alloc);
wy_error wy_machine_attach_context(wy_machine* self, wy_context* context);
void wy_machine_finalize_f(wy_machine* self);

wy_error wy_machine_find_symbol(wy_machine* self, const char* cstr, wy_symtab_entry* out);
wy_error wy_machine_insert_symbol(wy_machine* self, const char* cstr, wy_symtab_entry* out);

WY_END_DECLS

#endif
