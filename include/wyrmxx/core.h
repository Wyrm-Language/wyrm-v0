#ifndef WYRMX_WCORE_H_
#define WYRMX_WCORE_H_

#include <wyrm/core.h>
#include <wyrm/sys/string.h>

constexpr bool operator==(const wyrm_value& lhs, const wyrm_value& rhs)
{
    if (lhs.type != rhs.type) { return false; }
    if (wyrm_type_tag_is_gc(lhs.type)) { return lhs.data.gc_object == rhs.data.gc_object; }

    switch (lhs.type) {
    case WYRM_TYPE_TAG_NIL:
        return true;

    case WYRM_TYPE_TAG_WORD:
        return lhs.data.word == rhs.data.word;

    case WYRM_TYPE_TAG_UWORD:
        return lhs.data.uword == rhs.data.uword;

    default:
        return false;
    }
}


constexpr bool operator!=(const wyrm_value& lhs, const wyrm_value& rhs)
{
    return !(lhs == rhs);
}

#endif
