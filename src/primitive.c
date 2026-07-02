#include <wyrm.h>


bool wyrm_primitive_eq(wyrm_type_tag lhs_type, wyrm_primitive lhs, wyrm_type_tag rhs_type, wyrm_primitive rhs)
{
    // TODO: implement properly
    if (lhs_type != rhs_type) { return false; }
    return lhs.uword == rhs.uword;
}
