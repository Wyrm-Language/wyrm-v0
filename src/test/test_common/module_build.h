#ifndef WYRM_TEST_MODULE_BUILD_H_
#define WYRM_TEST_MODULE_BUILD_H_

#include <cstring>

#include <wyrm.h>
#include <wyrm/module.h>

// Helpers for hand-built wy_module fixtures.

/**
 * Give a hand-built module its init routine: a one-entry `functions` table
 * whose entry 0 has `nlocals` and `code_offset` (everything else zero, as the
 * loader requires of functions[0]). Allocated with wy_context_gc_alloc, as the
 * loader's table is, so the module finalizer frees it. Calling it again on a
 * module that already has a table just updates entry 0. Call it after setting
 * `code` / `code_len`.
 */
inline void wy_test_set_module_init(wy_context* ctx, wy_module* module, wy_u16 nlocals, wy_u32 code_offset = 0)
{
    if (module->function_count == 0) {
        auto* table = static_cast<wy_function_proto*>(wy_context_gc_alloc(ctx, sizeof(wy_function_proto)));
        std::memset(table, 0, sizeof(wy_function_proto));
        module->functions = table;
        module->function_count = 1;
    }
    module->functions[0].nlocals = nlocals;
    module->functions[0].code_offset = code_offset;
}

#endif
