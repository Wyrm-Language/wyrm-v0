#include <doctest/doctest.h>
#include <wyrm/internal_api.h>

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

static wyrm_exec_state test_exec_fn(wyrm_state* state)
{
    WYRM_UNUSED(state);
    return WYRM_EXEC_DONE;
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
        CHECK_EQ(wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(0)), WYRM_ERR_NOMEM);
    }

    TEST_CASE("null self returns WYRM_ERR_INVAL") {
        // Only non-_f functions perform null checks; _f functions use WYRM_ASSERT
        CHECK(wyrm_stack_pop(WYRM_NULL, WYRM_NULL, WYRM_NULL) == WYRM_ERR_INVAL);
    }

    TEST_CASE("push_continuation_f sets base and push_array_f adds args") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        // One value already on the stack (simulates caller context)
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(99));

        CHECK(wyrm_stack_push_continuation_f(&s, test_exec_fn) == WYRM_ERR_NONE);

        // base advances past the 2 header slots
        CHECK(s.base == s.entries_begin + 3);

        wyrm_value args[2];
        args[0].type = WYRM_TYPE_TAG_WORD; args[0].data = prim_uword(10);
        args[1].type = WYRM_TYPE_TAG_WORD; args[1].data = prim_uword(20);

        CHECK(wyrm_stack_push_array_f(&s, args, 2) == WYRM_ERR_NONE);

        // 1 existing + 2 headers + 2 args
        CHECK(s.top  == s.entries_begin + 5);
        // saved slot holds the old base (entries_begin)
        CHECK((s.base - 2)->data.value_ptr == s.entries_begin);
    }

    TEST_CASE("pop_continuation_f with no preserve collapses frame") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(1));

        wyrm_value args[1];
        args[0].type = WYRM_TYPE_TAG_WORD; args[0].data = prim_uword(42);
        REQUIRE_EQ(wyrm_stack_push_continuation_f(&s, test_exec_fn), WYRM_ERR_NONE);
        REQUIRE_EQ(wyrm_stack_push_array_f(&s, args, 1), WYRM_ERR_NONE);

        wyrm_exec_fn out_fn = WYRM_NULL;
        CHECK_EQ(wyrm_stack_pop_continuation_f(&s, &out_fn, 0), WYRM_ERR_NONE);
        CHECK(s.base == s.entries_begin);
        CHECK(s.top  == s.entries_begin + 1);  // original value survives
        CHECK(out_fn == test_exec_fn);
    }

    TEST_CASE("pop_continuation_f preserves results at call site") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        // Caller pushes one value
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(99));

        // Enter frame with 2 args
        wyrm_value args[2];
        args[0].type = WYRM_TYPE_TAG_WORD; args[0].data = prim_uword(1);
        args[1].type = WYRM_TYPE_TAG_WORD; args[1].data = prim_uword(2);
        REQUIRE(wyrm_stack_push_continuation_f(&s, test_exec_fn) == WYRM_ERR_NONE);
        REQUIRE(wyrm_stack_push_array_f(&s, args, 2) == WYRM_ERR_NONE);

        // Callee pushes 2 scratch values then 1 result at top
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(77));  // scratch
        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(55));  // result to preserve

        // Pop frame preserving 1 result
        wyrm_exec_fn out_fn = WYRM_NULL;
        CHECK(wyrm_stack_pop_continuation_f(&s, &out_fn, 1) == WYRM_ERR_NONE);
        CHECK_EQ(s.base, s.entries_begin);
        // Stack: [99, 55]
        CHECK_EQ(s.top, s.entries_begin + 2);
        CHECK_EQ(out_fn, test_exec_fn);

        wyrm_primitive out{};
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK_EQ(out.uword, 55);
        wyrm_stack_pop(&s, WYRM_NULL, &out); CHECK_EQ(out.uword, 99);
    }

    TEST_CASE("pop_continuation_f with too few entries returns WYRM_ERR_RANGE") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(0));
        REQUIRE_EQ(wyrm_stack_push_continuation_f(&s, test_exec_fn), WYRM_ERR_NONE);

        // Frame has 0 entries, preserving 1 should fail
        wyrm_exec_fn out_fn = WYRM_NULL;
        CHECK_EQ(wyrm_stack_pop_continuation_f(&s, &out_fn, 1), WYRM_ERR_RANGE);
    }

    TEST_CASE("pop_continuation_f with no outer frame returns WYRM_ERR_EMPTY") {
        wyrm_value mem[STACK_CAP];
        wyrm_stack s = make_stack(mem);

        wyrm_stack_push_f(&s, WYRM_TYPE_TAG_WORD, prim_uword(42));
        // base == entries_begin — no frame was started
        wyrm_exec_fn out_fn = WYRM_NULL;
        CHECK_EQ(wyrm_stack_pop_continuation_f(&s, &out_fn, 0), WYRM_ERR_EMPTY);
    }
}
