#include <doctest/doctest.h>
#include <wyrm.h>

static const int STACK_CAP = 16;

static wy_stack make_stack(wy_value* mem)
{
    wy_stack s;
    wy_stack_init_f(&s, mem, (wy_uword)STACK_CAP);
    return s;
}

static wy_primitive prim_uword(wy_uword v)
{
    wy_primitive p;
    p.uword = v;
    return p;
}


TEST_SUITE("stack") {
    TEST_CASE("push and pop round-trip") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        CHECK(wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(42)) == WY_ERR_NONE);
        CHECK(s.top == s.entries_begin + 1);

        wy_type_tag out_type = WY_TYPE_TAG_NIL;
        wy_primitive out_val = prim_uword(0);
        CHECK_EQ(wy_stack_pop(&s, &out_type, &out_val), WY_ERR_NONE);
        CHECK_EQ(out_type, WY_TYPE_TAG_WORD);
        CHECK_EQ(out_val.uword, 42);
        CHECK_EQ(s.top, s.entries_begin);
    }

    TEST_CASE("LIFO order") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(1));
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(2));
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(3));

        wy_primitive out{};
        wy_stack_pop(&s, WY_NULL, &out); CHECK(out.uword == 3);
        wy_stack_pop(&s, WY_NULL, &out); CHECK(out.uword == 2);
        wy_stack_pop(&s, WY_NULL, &out); CHECK(out.uword == 1);
    }

    TEST_CASE("pop from empty returns WY_ERR_EMPTY") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);
        wy_primitive out{};
        CHECK_EQ(wy_stack_pop(&s, WY_NULL, &out), WY_ERR_EMPTY);
    }

    TEST_CASE("push when full returns WY_ERR_NOMEM") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);
        for (int i = 0; i < STACK_CAP; i++) {
            REQUIRE_EQ(wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword((wy_uword)i)), WY_ERR_NONE);
        }
        CHECK_EQ(wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(0)), WY_ERR_STACK_OVERFLOW);
    }

    TEST_CASE("null self returns WY_ERR_INVAL") {
        // Only non-_f functions perform null checks; _f functions use WY_ASSERT
        CHECK(wy_stack_pop(WY_NULL, WY_NULL, WY_NULL) == WY_ERR_INVAL);
    }

    TEST_CASE("base_reset_args_f with no preserve collapses to the saved base") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        // Caller value, then a frame rebased at the current top
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(1));
        wy_value* saved_base = wy_stack_base_f(&s);
        wy_stack_base_restore_f(&s, wy_stack_top_f(&s));

        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(42));

        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 0), WY_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        CHECK_EQ(s.top, s.entries_begin + 1);  // original value survives
    }

    TEST_CASE("base_reset_args_f preserves results at the call site") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        // Caller pushes one value
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(99));

        // Enter frame with 2 args
        wy_value* saved_base = wy_stack_base_f(&s);
        wy_stack_base_restore_f(&s, wy_stack_top_f(&s));

        wy_value args[2];
        args[0].type = WY_TYPE_TAG_WORD; args[0].data = prim_uword(1);
        args[1].type = WY_TYPE_TAG_WORD; args[1].data = prim_uword(2);
        REQUIRE_EQ(wy_stack_push_array_f(&s, args, 2), WY_ERR_NONE);
        CHECK_EQ(wy_stack_arg_count_f(&s), 2);

        // Callee pushes a scratch value then 1 result at top
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(77));  // scratch
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(55));  // result to preserve

        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 1), WY_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        // Stack: [99, 55]
        CHECK_EQ(s.top, s.entries_begin + 2);

        wy_primitive out{};
        wy_stack_pop(&s, WY_NULL, &out); CHECK_EQ(out.uword, 55);
        wy_stack_pop(&s, WY_NULL, &out); CHECK_EQ(out.uword, 99);
    }

    TEST_CASE("base_reset_args_f with too few entries returns WY_ERR_RANGE") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(0));
        wy_value* saved_base = wy_stack_base_f(&s);
        wy_stack_base_restore_f(&s, wy_stack_top_f(&s));

        // Frame has 0 entries, preserving 1 should fail and leave the stack alone
        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 1), WY_ERR_RANGE);
        CHECK_EQ(s.base, s.entries_begin + 1);
        CHECK_EQ(s.top, s.entries_begin + 1);
    }

    TEST_CASE("base_reset_args_f keeps a full frame in place") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        wy_value* saved_base = wy_stack_base_f(&s);
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(7));
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(8));

        CHECK_EQ(wy_stack_base_reset_args_f(&s, saved_base, 2), WY_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        CHECK_EQ(s.top, s.entries_begin + 2);
        CHECK_EQ(s.entries_begin[0].data.uword, 7);
        CHECK_EQ(s.entries_begin[1].data.uword, 8);
    }
}
