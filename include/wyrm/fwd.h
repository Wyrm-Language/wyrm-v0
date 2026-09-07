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

WYRM_BEGIN_DECLS

struct wy_context;
struct wy_fiber;
struct wy_machine;
struct wy_module;
struct wy_object;
struct wy_stack;
struct wy_value;
union wy_primitive;

struct wyrm_allocator;
struct wyrm_allocator_vt;
struct wyrm_box;
struct wyrm_class;
struct wyrm_dict;
struct wyrm_dstruct;
struct wyrm_main_loop;
struct wyrm_main_loop_vt;
struct wyrm_object_list;
struct wyrm_object_type;
struct wyrm_pair;
struct wyrm_prototype;
struct wyrm_scope;
struct wyrm_state;
struct wyrm_string;
struct wy_work_area;

#ifndef __cplusplus
typedef struct wy_context wy_context;
typedef struct wy_fiber wy_fiber;
typedef struct wy_machine wy_machine;
typedef struct wy_module wy_module;
typedef struct wy_object wy_object;
typedef struct wy_stack wy_stack;
typedef struct wy_value wy_value;
typedef struct wy_work_area wy_work_area;
typedef union wy_primitive wy_primitive;

typedef struct wyrm_allocator wyrm_allocator;
typedef struct wyrm_allocator_vt wyrm_allocator_vt;
typedef struct wyrm_box wyrm_box;
typedef struct wyrm_class wyrm_class;
typedef struct wyrm_dict wyrm_dict;
typedef struct wyrm_dstruct wyrm_dstruct;
typedef struct wyrm_main_loop wyrm_main_loop;
typedef struct wyrm_main_loop_vt wyrm_main_loop_vt;
typedef struct wyrm_object_type wyrm_object_type;
typedef struct wyrm_pair wyrm_pair;
typedef struct wyrm_prototype wyrm_prototype;
typedef struct wyrm_state wyrm_state;
typedef struct wyrm_string wyrm_string;
#endif

/* Transitional aliases: wyrm_* spellings of the wy_* tags above. */
typedef struct wy_context wyrm_context;
typedef struct wy_fiber wyrm_fiber;
typedef struct wy_machine wyrm_machine;
typedef struct wy_object wyrm_object;
typedef struct wy_stack wyrm_stack;
typedef struct wy_value wyrm_value;
typedef union wy_primitive wyrm_primitive;
typedef wy_work_area wyrm_work_area;

typedef struct wyrm_allocator wy_allocator;

WYRM_END_DECLS

#endif
