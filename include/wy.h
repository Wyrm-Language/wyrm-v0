#ifndef WY_H_
#define WY_H_

#include <wyrm.h>

struct wy_ctx;

enum wy_allocator_type
{
    WY_ALLOCATOR_CMEM = 0,
};

struct wy_options
{
    enum wy_allocator_type allocator;
};


#ifndef __cplusplus
typedef struct wy_ctx wy_ctx;
typedef struct wy_options wy_options;
#endif

wy_ctx* wy_init(const wy_options* options);
struct wyrm_state* wy_get_primary_state(wy_ctx* ctx);
struct wyrm_context* wy_get_primary_context(wy_ctx* ctx);
struct wyrm_fiber* wy_get_primary_fiber(wy_ctx* ctx);
int wy_run(wy_ctx* ctx);
void wy_destroy(wy_ctx* ctx);


#endif
