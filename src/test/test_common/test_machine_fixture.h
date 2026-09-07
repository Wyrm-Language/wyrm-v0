#ifndef WYRM_TEST_MACHINE_FIXTURE_H_
#define WYRM_TEST_MACHINE_FIXTURE_H_

#include <doctest/doctest.h>
#include <wyrm.h>

#include <test_common/test_allocator_fixture.h>


class test_machine_failure : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};


struct wy_test_main_loop_state {
    wy_uword fired_count;
};


WY_INLINE bool wy_test_main_loop_count_cb(wy_primitive ud) {
    auto state = reinterpret_cast<wy_test_main_loop_state*>(ud.tagged_ptr);
    state->fired_count += 1;
    return false;
}

struct test_machine_fixture
{
    test_allocator_fixture allocator;
    wy_machine machine;

    test_machine_fixture()
        : machine{}
    {
        wy_machine_init_s(&machine, allocator.ptr());
    }

    wy_machine* get_machine_ptr() { return &machine; }
};

#endif
