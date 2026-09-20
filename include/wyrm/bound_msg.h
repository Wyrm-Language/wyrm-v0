#ifndef WYRM_BOUND_MSG_H_
#define WYRM_BOUND_MSG_H_

#include <wyrm/message.h>
#include <wyrm/object.h>
#include <wyrm/value.h>

WY_BEGIN_DECLS

struct wy_bound_msg;
#ifndef __cplusplus
typedef struct wy_bound_msg wy_bound_msg;
#endif

extern const wy_object_type wy_bound_msg_type;

/**
 * `getmsg`'s result (wyc-format.md §6.3: "the bound closure for `receiver !
 * name`, without calling it"): a receiver-bound, already-resolved
 * callable. `receiver` is the value `getmsg` bound (itself a tuple for a
 * multi-dispatch bind); `body` is the overload `wy_dispatch_resolve_f`
 * already chose for it - calling a BOUND_MSG value (design_c_vm.md §7's
 * construct-on-call/message-dispatch call sites) pushes `body` with
 * `receiver`'s value(s) as `this`, same as the original `recv ! name(...)`
 * dispatch would have. `msg` is kept only for error text (naming the
 * message a bound call fails to bind further arguments for).
 */
struct wy_bound_msg
{
    wy_object object;
    wy_value receiver;
    wy_message* msg;
    wy_value body;   /**< FUNCTION value */
};

wy_error wy_bound_msg_new_f(wy_context* context, wy_value receiver, wy_message* msg, wy_value body, wy_bound_msg** out);

WY_END_DECLS

#endif
