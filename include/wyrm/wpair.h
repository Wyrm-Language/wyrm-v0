#ifndef WYRM_WPAIR_H_
#define WYRM_WPAIR_H_

#include <wyrm/wcore.h>
#include <wyrm/wcontext.h>

WYRM_BEGIN_DECLS

extern const wyrm_object_type wyrm_pair_type;

/**
 * A pair data structure
 */
struct wyrm_pair
{
    wyrm_object object;
    wyrm_value car;
    wyrm_value cdr;
};


/**
 * @brief Construct new pair with both items unset
 */
WYRM_INLINE wyrm_pair* wyrm_pair_new_f(wyrm_context* context)
{
    wyrm_pair* pair = (wyrm_pair*) wyrm_context_gc_alloc(context, sizeof(wyrm_pair));
    if (pair == WYRM_NULL) { return WYRM_NULL; }

    pair->car = wyrm_value_Unset();
    pair->cdr = wyrm_value_Unset();

    wyrm_context_object_init_header_f(context, &pair->object, &wyrm_pair_type);
    return pair;
}


/**
 * @brief Construct pair as (cons head tail)
 */
WYRM_INLINE wyrm_pair* wyrm_pair_cons_f(wyrm_context* context, wyrm_value head, wyrm_value tail)
{
    wyrm_pair* pair = wyrm_pair_new_f(context);
    if (pair != WYRM_NULL) {
        pair->car = head;
        pair->cdr = tail;
    }
    return pair;
}


/**
 * @brief Get head of the pair
 */
WYRM_INLINE wyrm_value wyrm_pair_car_f(wyrm_pair* pair)
{
    return pair->car;
}


/**
 * @brief Get tail of the pair
 */
WYRM_INLINE wyrm_value wyrm_pair_cdr_f(wyrm_pair* pair)
{
    return pair->cdr;
}

WYRM_END_DECLS

#endif
