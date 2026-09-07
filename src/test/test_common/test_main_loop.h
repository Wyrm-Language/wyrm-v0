#ifndef WYRM_TEST_MAIN_LOOP_RIG_H_
#define WYRM_TEST_MAIN_LOOP_RIG_H_

#include <doctest/doctest.h>
#include <wyrm.h>

typedef struct wy_test_main_loop_state {
    wy_uword fired_count;
} wy_test_main_loop_state;

static inline bool wy_test_main_loop_count_cb(wy_primitive ud) {
    wy_test_main_loop_state *state = (wy_test_main_loop_state *)(uintptr_t)ud.tagged_ptr;
    state->fired_count += 1;
    return false;
}

static inline void wy_test_main_loop_wakeable_sanity(wy_main_loop* loop) {
    wy_test_main_loop_state state = {0};
    wy_primitive user_data = {0};
    wy_primitive source = {0};

    user_data.tagged_ptr = (wy_uintptr)&state;

    REQUIRE(
        wy_main_loop_add_wakeable(
            loop,
            &source,
            WY_PRIORITY_DEFAULT,
            wy_test_main_loop_count_cb,
            user_data) == WY_ERR_NONE);

    CHECK(wy_main_loop_trigger(loop, source) == WY_ERR_NONE);
    CHECK(wy_main_loop_iterate(loop, false) == WY_ERR_NONE);
    CHECK(state.fired_count == 1);

    CHECK(wy_main_loop_trigger(loop, source) == WY_ERR_INVAL);
}


static inline void wy_test_main_loop_timer_sanity(wy_main_loop* loop, uint32_t timer_ms) {
    wy_test_main_loop_state state = {0};
    wy_primitive user_data = {0};
    wy_primitive source = {0};

    user_data.tagged_ptr = (wy_uintptr)&state;

    REQUIRE(
        wy_main_loop_add_timer(
            loop,
            &source,
            timer_ms,
            WY_PRIORITY_DEFAULT,
            wy_test_main_loop_count_cb,
            user_data) == WY_ERR_NONE);

    CHECK(wy_main_loop_iterate(loop, true) == WY_ERR_NONE);
    CHECK(state.fired_count == 1);
}

#endif
