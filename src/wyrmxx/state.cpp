#include <wyrmxx/state.h>
#include <wyrmxx/except.h>

namespace wyrmxx
{
    state_::state_()
        : wyrm_state{}
    {
        wyrm_state_init_s(this);
    }

    state_::~state_()
    {
        wyrm_state_delete(this);
    }

    state::state(wyrm_allocator* allocator)
        : state{}
    {
        auto state_ptr = wyrm_state_new(allocator);
        if (!state_ptr) { throw out_of_memory{}; }

        self_ = state_ptr;
        release_ = true;
    }
}
