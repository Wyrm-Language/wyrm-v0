#include <wyrmxx/state.h>
#include <wyrmxx/except.h>

namespace wyrmxx
{
    state_::state_()
        : wy_state{}
    {
        wy_state_init_s(this);
    }

    state_::~state_()
    {
        wy_state_delete(this);
    }

    state::state(wy_allocator* allocator)
        : state{}
    {
        auto state_ptr = wy_state_new(allocator);
        if (!state_ptr) { throw out_of_memory{}; }

        self_ = state_ptr;
        release_ = true;
    }
}
