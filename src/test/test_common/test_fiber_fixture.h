#ifndef TEST_FIBER_FIXTURE_H_
#define TEST_FIBER_FIXTURE_H_

#include <wyrm.h>

class test_fiber_fixture
{
public:
    test_fiber_fixture()
    {
        wyrm_fiber_init(&fiber, stack, stack_size);
    }

    wyrm_fiber* ptr() { return &fiber; }

private:
    static constexpr auto stack_size = 1024;

    wyrm_fiber fiber;
    wyrm_value stack[stack_size];
};

#endif
