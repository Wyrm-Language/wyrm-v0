#ifndef WYRM_FWD_H_
#define WYRM_FWD_H_

#include <wyrm/sys/toolchain.h>

/* -------------------------------------------------------------------------
 * Consolidated Forward Declarations
 *
 * Every wyrm type is declared here and defined exactly once elsewhere. Type
 * headers include this rather than forward declaring for themselves, so a
 * header needing only a pointer never has to include the definition.
 * ------------------------------------------------------------------------- */

WY_BEGIN_DECLS

struct wy_allocator;
struct wy_allocator_vt;
struct wy_box;
struct wy_class;
struct wy_context;
struct wy_dict;
struct wy_dstruct;
struct wy_fiber;
struct wy_machine;
struct wy_main_loop;
struct wy_main_loop_vt;
struct wy_module;
struct wy_object;
struct wy_object_list;
struct wy_object_type;
struct wy_pair;
union  wy_primitive;
struct wy_prototype;
struct wy_scope;
struct wy_stack;
struct wy_state;
struct wy_string;
struct wy_value;
struct wy_work_area;

#ifndef __cplusplus
typedef struct wy_allocator wy_allocator;
typedef struct wy_allocator_vt wy_allocator_vt;
typedef struct wy_box wy_box;
typedef struct wy_class wy_class;
typedef struct wy_context wy_context;
typedef struct wy_dict wy_dict;
typedef struct wy_dstruct wy_dstruct;
typedef struct wy_fiber wy_fiber;
typedef struct wy_machine wy_machine;
typedef struct wy_main_loop wy_main_loop;
typedef struct wy_main_loop_vt wy_main_loop_vt;
typedef struct wy_module wy_module;
typedef struct wy_object wy_object;
typedef struct wy_object_type wy_object_type;
typedef struct wy_pair wy_pair;
typedef union  wy_primitive wy_primitive;
typedef struct wy_prototype wy_prototype;
typedef struct wy_stack wy_stack;
typedef struct wy_state wy_state;
typedef struct wy_string wy_string;
typedef struct wy_value wy_value;
typedef struct wy_work_area wy_work_area;
#endif

WY_END_DECLS

#endif
