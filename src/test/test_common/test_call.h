#ifndef WYRM_TEST_CALL_H_
#define WYRM_TEST_CALL_H_

#include <wyrm.h>
#include <wyrm/tuple.h>
#include <wyrm/vm.h>

/**
 * wy_vm_call_sync for tests that hand-pack bytecode. A call produces one
 * value (design runtime.md D12), so a test that wants to look at several
 * values builds a tuple of them and returns that. `nres` > 1 reads that
 * tuple back into out[0..nres), nil-filling a short one; `nres` 0 drops the
 * result and 1 passes it straight through.
 */
inline wy_error test_call_sync(wy_context* ctx, wy_value callee, const wy_value* args, wy_uword argc,
    wy_value* out, wy_uword nres)
{
    wy_value result = wy_value_nil();
    wy_error err = wy_vm_call_sync(ctx, callee, args, argc, &result);
    if (err != WY_ERR_NONE) { return err; }
    if (nres == 1) {
        out[0] = result;
    } else if (nres > 1) {
        if (result.type != WY_TYPE_TAG_TUPLE) { return WY_ERR_BAD_TYPE; }
        const wy_tuple* tup = (const wy_tuple*) result.data.gc_object;
        for (wy_uword i = 0; i < nres; i++) {
            out[i] = (i < tup->count) ? tup->items[i] : wy_value_nil();
        }
    }
    return WY_ERR_NONE;
}

#endif
