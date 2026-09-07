#ifndef WYRM_PRIMITIVE_H_
#define WYRM_PRIMITIVE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/thread_id.h>

#include <wyrm/exec_fn.h>
#include <wyrm/fwd.h>
#include <wyrm/symtab_entry.h>

WY_BEGIN_DECLS

#define WY_PRIMITIVE_PTR(dtype, v) ((dtype*) (v).ptr)



/**
 * @brief Primitive Types
 */
typedef enum wy_type_tag
{
    WY_TYPE_TAG_NIL = 0,

    WY_TYPE_TAG_WORD,
    WY_TYPE_TAG_UWORD,

    WY_TYPE_TAG_FUNCTION,
    WY_TYPE_TAG_SYMBOL,

    WY_TYPE_TAG_GC_PATH_START,

    WY_TYPE_TAG_ERROR,
    WY_TYPE_TAG_PAIR,
    WY_TYPE_TAG_BOX,
    WY_TYPE_TAG_OBJECT,
    WY_TYPE_TAG_STR,
    WY_TYPE_TAG_FIBER,
    WY_TYPE_TAG_DTYPE,
    WY_TYPE_TAG_CLASS,
    WY_TYPE_TAG_MODULE,

    WY_TYPE_TAG_TABLE
} wy_type_tag;



/**
 * Test whether a tag denotes a garbage collected object reference
 *
 * @param tag Type tag to test
 * @return true when the tagged primitive holds a wy_object pointer
 */
WY_INLINE bool wy_type_is_object(wy_type_tag tag)
{
    return tag >= WY_TYPE_TAG_GC_PATH_START;
}

/**
 * A machine register sized union acting as fundamental VM register.
 *
 * A wyrm primitive union aligns to the host machine size. A primitive must
 * ALWAYS exist with a type. The wy_value provides this pairing, but the
 * primitive may be used independently in some cases.
 */
union wy_primitive {
    wy_object* gc_object;

    wy_word word;
    wy_handle handle;
    wy_short s_word;
    wy_uword uword;
    wy_ushort s_uword[2];
    wy_float fp;
    wy_float_s fp_s;
    wy_uintptr tagged_ptr;
    wy_error error;
    wy_exec_fn cb;
    wy_atomic_word ref_count;
    wy_sys_thread_id thread_id;
    wy_value* value_ptr;
    wy_box* box_ptr;
    wy_symtab_entry symtab_entry;
    wy_pair* pair_ptr;
    void* ptr;
    bool flag;

    wy_string* str;
    wy_class* cls;
};

WY_INLINE wy_primitive wy_primitive_int(wy_word value) { const wy_primitive v = {.word = value}; return v; }
WY_INLINE wy_primitive wy_primitive_uword(wy_uword value) { const wy_primitive v = {.uword = value}; return v; }
WY_INLINE wy_primitive wy_primitive_ptr(void* value) { const wy_primitive v = {.ptr = value}; return v; }




WY_END_DECLS

#endif
