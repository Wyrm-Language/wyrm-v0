#ifndef WYRMXX_FIBER_H_
#define WYRMXX_FIBER_H_

#include <wyrm.h>

namespace wyrmxx
{
    class fiber
    {
    public:
        fiber(const fiber&) = delete;
        fiber& operator=(const fiber&) = delete;

        explicit fiber(wyrm_fiber* self) : fiber_{self} {}
        fiber(fiber&& other) noexcept : fiber_{other.fiber_} {}
        fiber& operator=(fiber&& other) noexcept { fiber_ = other.fiber_; return *this; }

        wyrm_fiber* fiber_;
    };

}

#endif
