#ifndef WYRM_TEST_MACHINE_FIXTURE_H_
#define WYRM_TEST_MACHINE_FIXTURE_H_

#include <doctest/doctest.h>
#include <wyrm.h>

#include <test_common/test_allocator_fixture.h>


class test_machine_failure : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};


struct wyrm_test_main_loop_state {
    wyrm_uword fired_count;
};


WYRM_INLINE bool wyrm_test_main_loop_count_cb(wyrm_primitive ud) {
    auto state = reinterpret_cast<wyrm_test_main_loop_state*>(ud.tagged_ptr);
    state->fired_count += 1;
    return false;
}

struct test_machine_fixture
{
    test_allocator_fixture allocator;
    wyrm_machine machine;

    test_machine_fixture()
        : machine{}
    {
        wyrm_machine_init_s(&machine, allocator.ptr());
    }

    wyrm_machine* get_machine_ptr() { return &machine; }
};

#endif
