#ifndef WYRMXX_STATE_H_
#define WYRMXX_STATE_H_

#include <utility>
#include <wyrm.h>

namespace wyrmxx
{
    class state_ final : public wyrm_state
    {
    public:
        state_(const state_&) = delete;
        state_& operator=(const state_&) = delete;

        state_();
        ~state_();
    };

    class state final
    {
    public:
        state(const state&) = delete;
        state& operator=(const state&) = delete;

        explicit state(wyrm_allocator* allocator);

        state(wyrm_state* ptr, bool release)
            : self_{ptr}, release_{release}
        {
        }

        ~state() {
            if (release_) { wyrm_state_delete(self_); }
        }

        state(state&& other) noexcept : state() {
            *this = std::move(other);
        }

        state& operator=(state&& other) noexcept {
            std::swap(self_, other.self_); std::swap(release_, other.release_);
            return *this;
        }

        operator wyrm_state*() const { return self_; }

    private:
        state() : self_{nullptr}, release_{false} {}

        wyrm_state* self_;
        bool release_;
    };
}

#endif
