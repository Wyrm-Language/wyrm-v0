#ifndef WYRM_INL_CONTEXT_H_
#define WYRM_INL_CONTEXT_H_

#include <wyrm/types.h>

WYRM_BEGIN_DECLS

/**
 * Get the root scope for the context
 *
 * @param context Context to access
 * @return Table associated with the root scope
 */
WYRM_INLINE wyrm_dict* wyrm_context_get_root_f(wyrm_context* context)
{
    WYRM_ASSERT(context != WYRM_NULL);
    return context->root;
}

/**
 * Set the root scope for the context
 *
 * @param context Context to access
 * @param root Table to use as root scope
 * @return WYRM_ERR_BUSY if already set, WYRM_ERR_NONE on success
 */
WYRM_INLINE wyrm_error wyrm_context_set_root_f(wyrm_context* context, wyrm_dict* root)
{
    WYRM_ASSERT(context != WYRM_NULL);
    if (context->root != WYRM_NULL) { return WYRM_ERR_BUSY; }
    context->root = root;
    return WYRM_ERR_NONE;
}

/**
 * Gab machine associated with context
 *
 * @param self Context
 * @return Machine associated with the context
 */
WYRM_INLINE wyrm_machine* wyrm_context_get_machine(wyrm_context* self)
{
    if (!self) { return WYRM_NULL; }
    return self->parent;
}


WYRM_END_DECLS

#endif
