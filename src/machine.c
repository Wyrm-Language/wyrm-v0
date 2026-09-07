#include <wyrm.h>

/* SCAFFOLDING */
enum
{
    WTEMP_SYMTAB_LEN = 8192 /* NOTE: likely too big on emb target, need more thought! */
};

struct wyrm_machine_symtab
{
    size_t alloc_size;
    char data[];
};


static inline wyrm_uword symtab_find_str(const char sym_str[], const char* content)
{
    wyrm_uword current = 0;
    while (sym_str[current] != 0) {
        if (wyrm_strcmp_f(&sym_str[current + 1], content) == 0) { return current; }
        current = current + sym_str[current] + 2;
    }
    return current;
}

wyrm_error wyrm_machine_init_s(wyrm_machine* self, wyrm_allocator* alloc)
{
    struct wyrm_machine_symtab* sym = WYRM_NULL;

    if (alloc != WYRM_NULL) {
        sym = wyrm_allocator_alloc_array(alloc, sizeof(struct wyrm_machine_symtab), sizeof(char), WTEMP_SYMTAB_LEN);

        if (!sym) {
            wyrm_allocator_free(alloc, sym);
            return WYRM_ERR_NOMEM;
        }
    }

    if (sym != WYRM_NULL) {
        sym->alloc_size = WTEMP_SYMTAB_LEN;
        sym->data[0] = 0;
    }

    self->allocator = alloc;
    self->context= WYRM_NULL;
    self->symtab = sym;

    return WYRM_ERR_NONE;
}

wyrm_error wyrm_machine_attach_context(wyrm_machine* self, wyrm_context* context)
{
    if (self == WYRM_NULL || context == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->context != WYRM_NULL) { /* todo: multithread support */ return WYRM_ERR_BUSY; }
    self->context = context;
    context->parent = self;

    /* TODO: should be setup with context */
    context->arena.allocator = self->allocator;

    return WYRM_ERR_NONE;
}

void wyrm_machine_finalize_f(wyrm_machine* self)
{
    if (self == WYRM_NULL) { return; }
    wyrm_allocator_free(self->allocator, self->symtab); self->symtab = WYRM_NULL;
}

wyrm_error wyrm_machine_find_symbol(wyrm_machine* self, const char* cstr, wyrm_symtab_entry* out)
{
    if (self == WYRM_NULL || out == WYRM_NULL) { return WYRM_ERR_INVAL; }
    if (self->symtab == WYRM_NULL) { *out = WYRM_NULL; return WYRM_ERR_KEY; }
    wyrm_uword offset = symtab_find_str(self->symtab->data, cstr);

    if (self->symtab->data[offset] == 0) {
        *out = WYRM_NULL; return WYRM_ERR_KEY;
    }
    *out = &self->symtab->data[offset];
    return WYRM_ERR_NONE;
}

wyrm_error wyrm_machine_insert_symbol(wyrm_machine* self, const char* cstr, wyrm_symtab_entry* out)
{
    if (self == WYRM_NULL || cstr == WYRM_NULL || self->symtab == WYRM_NULL) { return WYRM_ERR_INVAL; }
    wyrm_uword length = wyrm_strlen_f(cstr);

    if (length == 0) { return WYRM_ERR_INVAL; }
    if (length >= 128) { return WYRM_ERR_INVAL; }

    wyrm_uword offset = symtab_find_str(self->symtab->data, cstr);
    char* symtab_buffer = &self->symtab->data[offset];

    if (*symtab_buffer != 0) { *out = symtab_buffer; return WYRM_ERR_EXISTS; }

    wyrm_uword end = offset + length + 2;
    if (end >= self->symtab->alloc_size) { return WYRM_ERR_NOMEM; }

    wyrm_memcpy(&self->symtab->data[offset + 1], cstr, length);
    self->symtab->data[offset + 1 + length] = '\0';
    self->symtab->data[end] = 0;
    self->symtab->data[offset] = (char) length;
    *out = &self->symtab->data[offset];
    return WYRM_ERR_NONE;
}
