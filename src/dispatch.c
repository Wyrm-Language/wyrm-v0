#include <stdio.h>

#include "vm_internal.h"

#include <wyrm.h>
#include <wyrm/sys/string.h>

/**
 * The distance vector of one overload against `receivers[0..n)`
 * (design_c_vm.md §7's ranking rule): wildcard = WY_WILDCARD_DISTANCE;
 * CLASS constraint = ancestor distance via `wy_class_distance_f`, or no
 * match; PTYPE constraint = 0 on an exact tag match, else no match. An
 * arity mismatch is also no match.
 *
 * @return false if `ov` cannot apply to these receivers at all (arity
 *   mismatch or any position's constraint excludes it) - `out_dist` is
 *   only meaningful when this returns true.
 */
static bool overload_distance_f(const wy_overload* ov, const wy_value* receivers, wy_uword n, wy_u16* out_dist)
{
    if (ov->arity != n) { return false; }

    for (wy_uword k = 0; k < n; k++) {
        wy_value constraint = ov->types[k];

        if (constraint.type == WY_TYPE_TAG_NIL) {
            out_dist[k] = (wy_u16) WY_WILDCARD_DISTANCE;
            continue;
        }

        if (constraint.type == WY_TYPE_TAG_CLASS) {
            if (receivers[k].type != WY_TYPE_TAG_INSTANCE) { return false; }
            wy_instance* inst = (wy_instance*) receivers[k].data.gc_object;
            wy_uword d = wy_class_distance_f(inst->cls, (wy_class*) constraint.data.gc_object);
            if (d == WY_WILDCARD_DISTANCE) { return false; }
            out_dist[k] = (wy_u16) d;
            continue;
        }

        if (constraint.type == WY_TYPE_TAG_PTYPE) {
            if (receivers[k].type != (wy_type_tag) constraint.data.uword) { return false; }
            out_dist[k] = 0;
            continue;
        }

        /* Malformed types entry: reg_msg/class realisation never write
         * anything else into a types slot. */
        return false;
    }
    return true;
}

/** Lexicographic compare of two `n`-entry distance vectors. */
static int dist_cmp_f(const wy_u16* a, const wy_u16* b, wy_uword n)
{
    for (wy_uword k = 0; k < n; k++) {
        if (a[k] != b[k]) { return (a[k] < b[k]) ? -1 : 1; }
    }
    return 0;
}

wy_error wy_dispatch_resolve_f(wy_message* msg, const wy_value* receivers, wy_uword n,
    const wy_u16* exclude, const wy_overload** out, char* fault_msg, wy_uword fault_msg_size)
{
    if (n == 0 || n > WY_DISPATCH_MAX_RECEIVERS) { return WY_ERR_RANGE; }

    bool found = false;
    bool tie = false;
    wy_u16 best[WY_DISPATCH_MAX_RECEIVERS];
    const wy_overload* best_ov = WY_NULL;

    for (wy_uword i = 0; i < msg->overload_count; i++) {
        const wy_overload* ov = &msg->overloads[i];
        wy_u16 dist[WY_DISPATCH_MAX_RECEIVERS];
        if (!overload_distance_f(ov, receivers, n, dist)) { continue; }
        if (exclude != WY_NULL && dist_cmp_f(dist, exclude, n) <= 0) { continue; }

        if (!found) {
            wy_memcpy(best, dist, sizeof(wy_u16) * n);
            best_ov = ov;
            found = true;
            tie = false;
            continue;
        }
        int cmp = dist_cmp_f(dist, best, n);
        if (cmp < 0) {
            wy_memcpy(best, dist, sizeof(wy_u16) * n);
            best_ov = ov;
            tie = false;
        } else if (cmp == 0) {
            tie = true;
        }
    }

    const char* name = (msg->name != WY_NULL) ? msg->name : "<message>";
    if (!found) {
        snprintf(fault_msg, fault_msg_size,
            (exclude != WY_NULL) ? "no more general overload of '%s' for %u receiver(s)"
                                  : "no overload of '%s' matches %u receiver(s)",
            name, (unsigned) n);
        return WY_ERR_UNBOUND;
    }
    if (tie) {
        snprintf(fault_msg, fault_msg_size, "ambiguous overload for '%s': multiple equally-specific matches", name);
        return WY_ERR_AMBIGUOUS;
    }
    *out = best_ov;
    return WY_ERR_NONE;
}

wy_error wy_dispatch_body_distance_f(wy_message* msg, wy_value body, const wy_value* receivers, wy_uword n, wy_u16* out_dist)
{
    for (wy_uword i = 0; i < msg->overload_count; i++) {
        const wy_overload* ov = &msg->overloads[i];
        if (ov->body.type != body.type || ov->body.data.gc_object != body.data.gc_object) { continue; }
        if (overload_distance_f(ov, receivers, n, out_dist)) { return WY_ERR_NONE; }
    }
    return WY_ERR_UNBOUND;
}

wy_error wy_dispatch_single_instance_f(wy_message* msg, wy_instance* inst, wy_value* out_body)
{
    for (wy_class* c = inst->cls; c != WY_NULL; c = c->super) {
        for (wy_uword i = 0; i < c->msg_count; i++) {
            if (c->msg_map[i].msg == msg) {
                *out_body = c->msg_map[i].body;
                return WY_ERR_NONE;
            }
        }
    }
    return WY_ERR_UNBOUND;
}
