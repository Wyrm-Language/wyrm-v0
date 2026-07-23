#ifndef WY_PRIV_WY_CTX_H
#define WY_PRIV_WY_CTX_H

#include <wyrm.h>
#include <wy.h>

typedef void (*wy_internal_destructor)(void*);

struct wy_ctx
{
    void* allocator_primary_mem;
    wy_internal_destructor allocator_destructor;
    wyrm_allocator* allocator;
    wyrm_state primary_state;

    wyrm_machine machine;

    wyrm_main_loop* primary_main_loop;
    void* primary_main_loop_mem;
    wy_internal_destructor primary_main_loop_destructor;

    wyrm_context primary_context;

};

#endif
