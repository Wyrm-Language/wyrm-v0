#ifndef WYRM_TEST_MAIN_LOOP_RIG_H_
#define WYRM_TEST_MAIN_LOOP_RIG_H_

#include <doctest/doctest.h>

#include <wyrm/internal_api.h>

typedef struct wyrm_test_main_loop_state {
    wyrm_uword fired_count;
} wyrm_test_main_loop_state;

static inline bool wyrm_test_main_loop_count_cb(wyrm_primitive ud) {
    wyrm_test_main_loop_state *state = (wyrm_test_main_loop_state *)(uintptr_t)ud.tagged_ptr;
    state->fired_count += 1;
    return false;
}

static inline void wyrm_test_main_loop_wakeable_sanity(wyrm_main_loop* loop) {
    wyrm_test_main_loop_state state = {0};
    wyrm_primitive user_data = {0};
    wyrm_primitive source = {0};

    user_data.tagged_ptr = (wyrm_uintptr)&state;

    REQUIRE(
        wyrm_main_loop_add_wakeable(
            loop,
            &source,
            WYRM_PRIORITY_DEFAULT,
            wyrm_test_main_loop_count_cb,
            user_data) == WYRM_ERR_NONE);

    CHECK(wyrm_main_loop_trigger(loop, source) == WYRM_ERR_NONE);
    CHECK(wyrm_main_loop_iterate(loop, false) == WYRM_ERR_NONE);
    CHECK(state.fired_count == 1);

    CHECK(wyrm_main_loop_trigger(loop, source) == WYRM_ERR_INVAL);
}


static inline void wyrm_test_main_loop_timer_sanity(wyrm_main_loop* loop, uint32_t timer_ms) {
    wyrm_test_main_loop_state state = {0};
    wyrm_primitive user_data = {0};
    wyrm_primitive source = {0};

    user_data.tagged_ptr = (wyrm_uintptr)&state;

    REQUIRE(
        wyrm_main_loop_add_timer(
            loop,
            &source,
            timer_ms,
            WYRM_PRIORITY_DEFAULT,
            wyrm_test_main_loop_count_cb,
            user_data) == WYRM_ERR_NONE);

    CHECK(wyrm_main_loop_iterate(loop, true) == WYRM_ERR_NONE);
    CHECK(state.fired_count == 1);
}

#endif
