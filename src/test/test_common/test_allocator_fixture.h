#ifndef WYRM_TEST_COMMON_TEST_ALLOCATOR_FIXTURE_H
#define WYRM_TEST_COMMON_TEST_ALLOCATOR_FIXTURE_H

#include <doctest/doctest.h>

#include <algorithm>
#include <wyrm/types.h>
#include <list>
#include <cstdlib>
#include <stdexcept>

class test_allocator_failure : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class test_allocator_fixture
{
public:
    struct ext_alloc
    {
        wyrm_allocator a;
        test_allocator_fixture* self;
    };

    test_allocator_fixture()
        : allocator_ { .a = { .clz = &vt }, .self = this }
    {
    }

    ~test_allocator_fixture()
    {
        for (auto p : allocations_) {
            std::free(p);
        }
    }

    ext_alloc& allocator() { return allocator_; }
    wyrm_allocator* ptr() { return &allocator_.a; }
    ext_alloc allocator_;

    bool cleared() const { return allocations_.empty(); }
    void check() { REQUIRE(cleared()); }

private:
    static test_allocator_fixture& get_self(wyrm_allocator* self)
    {
        return *reinterpret_cast<ext_alloc*>(self)->self;
    }

    bool is_valid(void* buffer) const
    {
        return std::find(allocations_.begin(), allocations_.end(), buffer) != allocations_.end();
    }

    static void* n_alloc(wyrm_allocator* self, wyrm_uword len)
    {
        auto& thiz = get_self(self);
        auto buf = std::malloc(len);
        thiz.allocations_.push_back(buf);
        return buf;
    }

    static void* n_realloc(wyrm_allocator* self, void* buffer, wyrm_uword len)
    {
        auto& thiz = get_self(self);

        if (buffer) {
            if (!thiz.is_valid(buffer)) { throw test_allocator_failure("realloc of unallocated buffer"); }
            thiz.allocations_.remove(buffer);
        }

        auto new_buf = std::realloc(buffer, len);
        if (new_buf) {
            thiz.allocations_.push_back(new_buf);
        } else if (buffer) {
            thiz.allocations_.push_back(buffer);
        }

        return new_buf;
    }

    static void n_free(wyrm_allocator* self, void* buffer)
    {
        auto& thiz = get_self(self);
        if (buffer) {
            if (!thiz.is_valid(buffer)) { throw test_allocator_failure("double free detected"); }
            thiz.allocations_.remove(buffer);
            std::free(buffer);
        }
    }

    static inline wyrm_allocator_vt vt = {
        .alloc = n_alloc,
        .realloc = n_realloc,
        .free = n_free
    };
    std::list<void*> allocations_;
};

#endif
