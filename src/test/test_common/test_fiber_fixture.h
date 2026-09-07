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
        fiber = wy_fiber_create(get_context_ptr(), stack_size, frame_count);
        wy_context_attach_fiber(get_context_ptr(), fiber);
    }

    ~test_fiber_fixture()
    {
    }


    wy_fiber* get_fiber_ptr() const { return fiber; }

    operator wy_fiber*() const { return fiber; }

private:
    static constexpr auto stack_size = 1024;
    static constexpr auto frame_count = 1024;

    wy_fiber* fiber;
};

#endif
