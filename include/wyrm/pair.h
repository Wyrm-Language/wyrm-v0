#ifndef WYRM_WPAIR_H_
#define WYRM_WPAIR_H_

#include <wyrm/object.h>
#include <wyrm/value.h>
#include <wyrm/context.h>

WY_BEGIN_DECLS

extern const wy_object_type wy_pair_type;

/**
 * A pair data structure
 */
struct wy_pair
{
    wy_object object;
    wy_value car;
    wy_value cdr;
};


/**
 * @brief Construct new pair with both items unset
 */
WY_INLINE wy_pair* wy_pair_new_f(wy_context* context)
{
    wy_pair* pair = (wy_pair*) wy_context_gc_alloc(context, sizeof(wy_pair));
    if (pair == WY_NULL) { return WY_NULL; }

    pair->car = wy_value_Unset();
    pair->cdr = wy_value_Unset();

    wy_context_object_init_header_f(context, &pair->object, &wy_pair_type);
    return pair;
}


/**
 * @brief Construct pair as (cons head tail)
 */
WY_INLINE wy_pair* wy_pair_cons_f(wy_context* context, wy_value head, wy_value tail)
{
    wy_pair* pair = wy_pair_new_f(context);
    if (pair != WY_NULL) {
        pair->car = head;
        pair->cdr = tail;
    }
    return pair;
}


/**
 * @brief Get head of the pair
 */
WY_INLINE wy_value wy_pair_car_f(wy_pair* pair)
{
    return pair->car;
}


/**
 * @brief Get tail of the pair
 */
WY_INLINE wy_value wy_pair_cdr_f(wy_pair* pair)
{
    return pair->cdr;
}

WY_END_DECLS

#endif
