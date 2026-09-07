#ifndef WYRM_MACHINE_H_
#define WYRM_MACHINE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>
#include <wyrm/fwd.h>
#include <wyrm/symtab_entry.h>

WYRM_BEGIN_DECLS

struct wyrm_machine_symtab;

struct wy_machine
{
    wyrm_allocator* allocator;
    wy_context* context;

    struct wyrm_machine_symtab* symtab;
};


wy_error wyrm_machine_init_s(wy_machine* self, wyrm_allocator* alloc);
wy_error wyrm_machine_attach_context(wy_machine* self, wy_context* context);
void wyrm_machine_finalize_f(wy_machine* self);

wy_error wyrm_machine_find_symbol(wyrm_machine* self, const char* cstr, wyrm_symtab_entry* out);
wy_error wyrm_machine_insert_symbol(wyrm_machine* self, const char* cstr, wyrm_symtab_entry* out);

WYRM_END_DECLS

#endif
