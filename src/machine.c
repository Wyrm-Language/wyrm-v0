#include <wyrm.h>

/* SCAFFOLDING */
enum
{
    WTEMP_SYMTAB_LEN = 8192 /* NOTE: likely too big on emb target, need more thought! */
};

struct wy_machine_symtab
{
    size_t alloc_size;
    char data[];
};


static inline wy_uword symtab_find_str(const char sym_str[], const char* content)
{
    wy_uword current = 0;
    while (sym_str[current] != 0) {
        if (wy_strcmp_f(&sym_str[current + 1], content) == 0) { return current; }
        current = current + sym_str[current] + 2;
    }
    return current;
}

wy_error wy_machine_init_s(wy_machine* self, wy_allocator* alloc)
{
    struct wy_machine_symtab* sym = WY_NULL;

    if (alloc != WY_NULL) {
        sym = wy_allocator_alloc_array(alloc, sizeof(struct wy_machine_symtab), sizeof(char), WTEMP_SYMTAB_LEN);

        if (!sym) {
            wy_allocator_free(alloc, sym);
            return WY_ERR_NOMEM;
        }
    }

    if (sym != WY_NULL) {
        sym->alloc_size = WTEMP_SYMTAB_LEN;
        sym->data[0] = 0;
    }

    self->allocator = alloc;
    self->context= WY_NULL;
    self->symtab = sym;

    return WY_ERR_NONE;
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
    wy_allocator_free(self->allocator, self->symtab); self->symtab = WY_NULL;
}

wy_error wy_machine_find_symbol(wy_machine* self, const char* cstr, wy_symtab_entry* out)
{
    if (self == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (self->symtab == WY_NULL) { *out = WY_NULL; return WY_ERR_KEY; }
    wy_uword offset = symtab_find_str(self->symtab->data, cstr);

    if (self->symtab->data[offset] == 0) {
        *out = WY_NULL; return WY_ERR_KEY;
    }
    *out = &self->symtab->data[offset + 1];  // past the length-prefix byte, at the text itself
    return WY_ERR_NONE;
}

wy_error wy_machine_insert_symbol(wy_machine* self, const char* cstr, wy_symtab_entry* out)
{
    if (self == WY_NULL || cstr == WY_NULL || self->symtab == WY_NULL) { return WY_ERR_INVAL; }
    wy_uword length = wy_strlen_f(cstr);

    if (length == 0) { return WY_ERR_INVAL; }
    if (length >= 128) { return WY_ERR_INVAL; }

    wy_uword offset = symtab_find_str(self->symtab->data, cstr);
    char* symtab_buffer = &self->symtab->data[offset];

    if (*symtab_buffer != 0) { *out = &self->symtab->data[offset + 1]; return WY_ERR_EXISTS; }

    wy_uword end = offset + length + 2;
    if (end >= self->symtab->alloc_size) { return WY_ERR_NOMEM; }

    wy_memcpy(&self->symtab->data[offset + 1], cstr, length);
    self->symtab->data[offset + 1 + length] = '\0';
    self->symtab->data[end] = 0;
    self->symtab->data[offset] = (char) length;
    *out = &self->symtab->data[offset + 1];  // past the length-prefix byte, at the text itself
    return WY_ERR_NONE;
}
