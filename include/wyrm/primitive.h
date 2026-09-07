#ifndef WYRM_PRIMITIVE_H_
#define WYRM_PRIMITIVE_H_

#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/atomics.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/thread_id.h>

#include <wyrm/exec_fn.h>
#include <wyrm/fwd.h>
#include <wyrm/symtab_entry.h>

WYRM_BEGIN_DECLS

/**
 * @brief Primitive Types
 */
typedef enum wyrm_type_tag
{
    WYRM_TYPE_TAG_NIL = 0,

    WYRM_TYPE_TAG_WORD,
    WYRM_TYPE_TAG_UWORD,

    WYRM_TYPE_TAG_FUNCTION,
    WYRM_TYPE_TAG_SYMBOL,

    WYRM_TYPE_TAG_GC_PATH_START,

    WYRM_TYPE_TAG_ERROR,
    WYRM_TYPE_TAG_PAIR,
    WYRM_TYPE_TAG_BOX,
    WYRM_TYPE_TAG_OBJECT,
    WYRM_TYPE_TAG_STR,
    WYRM_TYPE_TAG_FIBER,
    WYRM_TYPE_TAG_DTYPE,
    WYRM_TYPE_TAG_CLASS,
    WYRM_TYPE_TAG_MODULE,

    WYRM_TYPE_TAG_TABLE
} wyrm_type_tag;

/**
 * Test whether a tag denotes a garbage collected object reference
 *
 * @param tag Type tag to test
 * @return true when the tagged primitive holds a wy_object pointer
 */
WYRM_INLINE bool wy_type_is_object(wyrm_type_tag tag)
{
    return tag >= WYRM_TYPE_TAG_GC_PATH_START;
}

/**
 * A machine register sized union acting as fundamental VM register.
 *
 * A wyrm primitive union aligns to the host machine size. A primitive must
 * ALWAYS exist with a type. The wyrm_value provides this pairing, but the
 * primitive may be used independently in some cases.
 */
union wy_primitive {
    wy_object* gc_object;

    wyrm_word word;
    wyrm_handle handle;
    wyrm_short s_word;
    wyrm_uword uword;
    wyrm_ushort s_uword[2];
    wyrm_float fp;
    wyrm_float_s fp_s;
    wyrm_uintptr tagged_ptr;
    wyrm_error error;
    wyrm_exec_fn cb;
    wyrm_atomic_word ref_count;
    wyrm_sys_thread_id thread_id;
    wy_value* value_ptr;
    wyrm_box* box_ptr;
    wyrm_symtab_entry symtab_entry;
    wyrm_pair* pair_ptr;
    void* ptr;
    bool flag;

    wyrm_string* str;
    wyrm_class* cls;
};

WYRM_END_DECLS

#endif
