#ifndef TEST_FIBER_FIXTURE_H_
#define TEST_FIBER_FIXTURE_H_

#include <wyrm.h>
#include <wyrm/fiber.h>
#include <test_common/test_context_fixture.h>

class test_fiber_fixture
    : public test_context_fixture
{
public:
    test_fiber_fixture()
        : fiber{}
    {
        fiber = wyrm_fiber_create(get_context_ptr(), stack_size);
        wyrm_context_attach_fiber(get_context_ptr(), fiber);
    }

    ~test_fiber_fixture()
    {
    }


    operator wyrm_fiber*() const { return fiber; }

private:
    static constexpr auto stack_size = 1024;

    wyrm_fiber* fiber;
};

#endif
