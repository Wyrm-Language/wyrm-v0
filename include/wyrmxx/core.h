#ifndef WYRMX_WCORE_H_
#define WYRMX_WCORE_H_

#include <wyrm/core.h>
#include <wyrm/sys/string.h>

constexpr bool operator==(const wy_value& lhs, const wy_value& rhs)
{
    if (lhs.type != rhs.type) { return false; }
    if (wy_type_is_object(lhs.type)) { return lhs.data.gc_object == rhs.data.gc_object; }

    switch (lhs.type) {
    case WY_TYPE_TAG_NIL:
        return true;

    case WY_TYPE_TAG_WORD:
        return lhs.data.word == rhs.data.word;

    case WY_TYPE_TAG_UWORD:
        return lhs.data.uword == rhs.data.uword;

    default:
        return false;
    }
}


constexpr bool operator!=(const wy_value& lhs, const wy_value& rhs)
{
    return !(lhs == rhs);
}

#endif
