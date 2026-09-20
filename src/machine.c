#include <wyrm.h>

wy_error wy_machine_init_s(wy_machine* self, wy_allocator* alloc)
{
    self->allocator = alloc;
    self->context = WY_NULL;

    if (alloc == WY_NULL) { return WY_ERR_INVAL; }

    return wy_symtab_init_f(&self->symtab, alloc);
}

wy_error wy_machine_attach_context(wy_machine* self, wy_context* context)
{
    if (self == WY_NULL || context == WY_NULL) { return WY_ERR_INVAL; }
    if (self->context != WY_NULL) { /* todo: multithread support */ return WY_ERR_BUSY; }
    self->context = context;
    context->parent = self;

    /* TODO: should be setup with context */
    context->arena.allocator = self->allocator;

    return WY_ERR_NONE;
}

void wy_machine_finalize_f(wy_machine* self)
{
    if (self == WY_NULL) { return; }
    wy_symtab_finalize_f(&self->symtab);
}

wy_error wy_machine_find_symbol(wy_machine* self, const char* cstr, wy_symtab_entry* out)
{
    if (self == WY_NULL || out == WY_NULL || cstr == WY_NULL) { return WY_ERR_INVAL; }

    wy_symbol found = wy_symtab_lookup(&self->symtab, cstr, wy_strlen_f(cstr));
    if (found == WY_SYMBOL_INVALID) { *out = WY_NULL; return WY_ERR_KEY; }
    *out = found;
    return WY_ERR_NONE;
}

wy_error wy_machine_insert_symbol(wy_machine* self, const char* cstr, wy_symtab_entry* out)
{
    if (self == WY_NULL || cstr == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    wy_uword length = wy_strlen_f(cstr);

    if (length == 0) { return WY_ERR_INVAL; }

    bool existed = wy_symtab_lookup(&self->symtab, cstr, length) != WY_SYMBOL_INVALID;

    wy_symbol interned = wy_symtab_intern(&self->symtab, cstr, length);
    if (interned == WY_SYMBOL_INVALID) { return WY_ERR_NOMEM; }

    *out = interned;
    return existed ? WY_ERR_EXISTS : WY_ERR_NONE;
}
