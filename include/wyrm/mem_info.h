#ifndef WYRM_MEM_INFO_H_
#define WYRM_MEM_INFO_H_

#include <wyrm/sys/toolchain.h>

WYRM_BEGIN_DECLS

enum
{
    WY_MEM_INFO_ALLOC_STATIC = 1
};

struct wy_mem_info
{
    wy_uword flags;
    void* begin;
    void* end;
};

#ifndef __cplusplus
typedef struct wy_mem_info wy_mem_info;
#endif

#define WY_MEM_INFO_BEGIN_PTR(type, info) ((type*) ((info)->begin))
#define WY_MEM_INFO_END_PTR(type, info) (((type*) ((info)->end)))
#define WY_MEM_INFO_COUNT(type, info) (wy_mem_info_count_f((info), sizeof(type)))
#define WY_MEM_INFO_TOP_NOT_AT_END(type, top, info) (wy_mem_info_capacity_at_ptr((info), (void*) (top)) >= sizeof(type))

WYRM_INLINE void wy_mem_info_init_empty_s(wy_mem_info* mem_info)
{
    mem_info->begin = WYRM_NULL;
    mem_info->end = WYRM_NULL;
    mem_info->flags = 0;
}

WYRM_INLINE wy_uword wy_mem_info_sz_f(const wy_mem_info* mem_info)
{
    return (wy_uword) ((char*) mem_info->end - (char*) mem_info->begin);
}

WYRM_INLINE wy_uword wy_mem_info_count_f(const wy_mem_info* mem_info, wy_uword block_sz)
{
    return wy_mem_info_sz_f(mem_info) / block_sz;
}

WYRM_INLINE wy_uword wy_mem_info_capacity_at_ptr(const wy_mem_info* mem_info, void* ptr)
{
    return (wy_uword) ((char*) mem_info->end - (char*) ptr);
}

WYRM_INLINE bool wy_mem_info_is_static_f(const wy_mem_info* mem_info)
{
    return (bool) (mem_info->flags & WY_MEM_INFO_ALLOC_STATIC);
}

WYRM_INLINE wy_uword wy_mem_info_block_count_sz_f(wy_uword block_sz, wy_uword count)
{
    return block_sz * count;
}

WYRM_END_DECLS

#endif
