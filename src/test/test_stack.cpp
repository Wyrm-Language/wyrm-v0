#include <doctest/doctest.h>
#include <wyrm.h>

static const int STACK_CAP = 16;

static wyrm_stack make_stack(wyrm_value* mem)
{
    wyrm_stack s;
    wyrm_stack_init_f(&s, mem, (wyrm_uword)STACK_CAP);
    return s;
}

static wyrm_primitive prim_uword(wyrm_uword v)
{
    wyrm_primitive p;
    p.uword = v;
    return p;
}


TEST_SUITE("stack") {
    TEST_CASE("push and pop round-trip") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        CHECK(wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(42)) == WYRM_ERR_NONE);
        CHECK(s.top == s.entries_begin + 1);

        wyrm_type_tag out_type = WYRM_TYPE_TAG_NIL;
        wyrm_primitive out_val = prim_uword(0);
        CHECK_EQ(wyrm_stack_pop(&s, &out_type, &out_val), WYRM_ERR_NONE);
        CHECK_EQ(out_type, WYRM_TYPE_TAG_WORD);
        CHECK_EQ(out_val.uword, 42);
        CHECK_EQ(s.top, s.entries_begin);
    }

    TEST_CASE("LIFO order") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(1));
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(2));
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(3));

        wyrm_primitive out{};
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK(out.uword == 3);
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK(out.uword == 2);
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK(out.uword == 1);
    }

    TEST_CASE("pop from empty returns WYRM_ERR_EMPTY") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);
        wyrm_primitive out{};
        CHECK_EQ(wyrm_stack_pop(&s, WYRM_NULL, &out), WYRM_ERR_EMPTY);
    }

    TEST_CASE("push when full returns WYRM_ERR_NOMEM") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);
        for (int i = 0; i < STACK_CAP; i++) {
            REQUIRE_EQ(wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword((wyrm_uword)i)), WYRM_ERR_NONE);
        }
        CHECK_EQ(wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(0)), WYRM_ERR_STACK_OVERFLOW);
    }

    TEST_CASE("null self returns WYRM_ERR_INVAL") {
        // Only non-_f functions perform null checks; _f functions use WYRM_ASSERT
        CHECK(wyrm_stack_pop(WYRM_NULL, WYRM_NULL, WYRM_NULL) == WYRM_ERR_INVAL);
    }

    TEST_CASE("base_reset_args_f with no preserve collapses to the saved base") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        // Caller value, then a frame rebased at the current top
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(1));
        wyrm_value* saved_base = wyrm_stack_base_f(&s);
        wyrm_stack_base_restore_f(&s, wyrm_stack_top_f(&s));

        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(42));

        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 0), WYRM_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        CHECK_EQ(s.top, s.entries_begin + 1);  // original value survives
    }

    TEST_CASE("base_reset_args_f preserves results at the call site") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        // Caller pushes one value
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(99));

        // Enter frame with 2 args
        wyrm_value* saved_base = wyrm_stack_base_f(&s);
        wyrm_stack_base_restore_f(&s, wyrm_stack_top_f(&s));

        wyrm_value args[2];
        args[0].type = WYRM_TYPE_TAG_WORD; args[0].data = prim_uword(1);
        args[1].type = WYRM_TYPE_TAG_WORD; args[1].data = prim_uword(2);
        REQUIRE_EQ(wyrm_stack_push_array_f(&s, args, 2), WYRM_ERR_NONE);
        CHECK_EQ(wyrm_stack_arg_count_f(&s), 2);

        // Callee pushes a scratch value then 1 result at top
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(77));  // scratch
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(55));  // result to preserve

        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 1), WYRM_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        // Stack: [99, 55]
        CHECK_EQ(s.top, s.entries_begin + 2);

        wyrm_primitive out{};
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK_EQ(out.uword, 55);
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK_EQ(out.uword, 99);
    }

    TEST_CASE("base_reset_args_f with too few entries returns WYRM_ERR_RANGE") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(0));
        wyrm_value* saved_base = wyrm_stack_base_f(&s);
        wyrm_stack_base_restore_f(&s, wyrm_stack_top_f(&s));

        // Frame has 0 entries, preserving 1 should fail and leave the stack alone
        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 1), WYRM_ERR_RANGE);
        CHECK_EQ(s.base, s.entries_begin + 1);
        CHECK_EQ(s.top, s.entries_begin + 1);
    }

    TEST_CASE("base_reset_args_f keeps a full frame in place") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        wyrm_value* saved_base = wyrm_stack_base_f(&s);
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(7));
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(8));

        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 2), WYRM_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        CHECK_EQ(s.top, s.entries_begin + 2);
        CHECK_EQ(s.entries_begin[0].data.uword, 7);
        CHECK_EQ(s.entries_begin[1].data.uword, 8);
    }
}
