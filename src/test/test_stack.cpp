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

    TEST_CASE("reserve_f marks the reserved slots unset") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        // Poison the region so an uninitialized reserve would be visible
        for (int i = 0; i < STACK_CAP; i++) {
            mem[i].type = WY_TYPE_TAG_OBJECT;
            mem[i].data = prim_uword(0xbadbad);
        }
        s.top = s.entries_begin;

        REQUIRE_EQ(wy_stack_reserve_f(&s, 3), WY_ERR_NONE);
        CHECK_EQ(s.top, s.entries_begin + 3);
        for (int i = 0; i < 3; i++) {
            // Unset carries an object tag, so the collector must see the null
            CHECK_EQ(s.entries_begin[i].type, wy_value_unset().type);
            CHECK_EQ(s.entries_begin[i].data.gc_object, WY_NULL);
            CHECK_FALSE(wy_value_is_gc_ref_f(s.entries_begin[i]));
        }
    }

    TEST_CASE("reserve_f past capacity returns WY_ERR_STACK_OVERFLOW") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        CHECK_EQ(wy_stack_reserve_f(&s, STACK_CAP + 1), WY_ERR_STACK_OVERFLOW);
        CHECK_EQ(s.top, s.entries_begin);

        CHECK_EQ(wy_stack_reserve_f(&s, STACK_CAP), WY_ERR_NONE);
        CHECK_EQ(wy_stack_reserve_f(&s, 1), WY_ERR_STACK_OVERFLOW);
    }

    TEST_CASE("replace_frame_f moves the tail-call arguments to the base") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        // Caller value, then a frame rebased at the current top
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(99));
        wy_stack_base_restore_f(&s, wy_stack_top_f(&s));

        // Old arguments and scratch, then the new arguments on top
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(1));
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(77));
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(10));
        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(20));

        CHECK_EQ(wy_stack_replace_frame_f(&s, 2), WY_ERR_NONE);

        // Base is unchanged; the frame now holds just the new arguments
        CHECK_EQ(s.base, s.entries_begin + 1);
        CHECK_EQ(wy_stack_arg_count_f(&s), 2);
        CHECK_EQ(s.base[0].data.uword, 10);
        CHECK_EQ(s.base[1].data.uword, 20);
        CHECK_EQ(s.entries_begin[0].data.uword, 99);
    }

    TEST_CASE("replace_frame_f with too few entries returns WY_ERR_RANGE") {
        wy_value mem[STACK_CAP];
        wy_stack s = make_stack(mem);

        wy_stack_push_f(&s, WY_TYPE_TAG_WORD, prim_uword(0));
        wy_stack_base_restore_f(&s, wy_stack_top_f(&s));

        // Frame holds no values, so keeping 1 must fail and change nothing
        CHECK_EQ(wy_stack_replace_frame_f(&s, 1), WY_ERR_RANGE);
        CHECK_EQ(s.base, s.entries_begin + 1);
        CHECK_EQ(s.top, s.entries_begin + 1);
    }
}
