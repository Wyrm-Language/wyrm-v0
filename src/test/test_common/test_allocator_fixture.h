#ifndef WYRM_TEST_COMMON_TEST_ALLOCATOR_FIXTURE_H
#define WYRM_TEST_COMMON_TEST_ALLOCATOR_FIXTURE_H

#include <doctest/doctest.h>

#include <algorithm>
#include <list>
#include <cstdlib>
#include <stdexcept>

#include <wyrmxx/allocator.h>

class test_allocator_failure : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class test_allocator_fixture
{
public:
    struct ext_alloc
    {
        wy_allocator a;
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

    wyrmxx::allocator get() { return wyrmxx::allocator{ptr()}; }
    operator wy_allocator*() & { return &allocator_.a; }

    ext_alloc& allocator() { return allocator_; }
    wy_allocator* ptr() { return &allocator_.a; }
    ext_alloc allocator_;

    bool cleared() const { return allocations_.empty(); }
    void check() { REQUIRE(cleared()); }
    void check_watched_free() { REQUIRE(must_free_.empty()); }

    void set_locked(bool locked) { locked_ = locked; }

    template<typename T>
    void watch(T* var)
    {
        void* vptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(var));

        auto ll = std::find(must_free_.begin(), must_free_.end(), vptr);
        if (ll == must_free_.end())
        {
            must_free_.push_back(vptr);
        }
    }


private:
    static test_allocator_fixture& get_self(wy_allocator* self)
    {
        return *reinterpret_cast<ext_alloc*>(self)->self;
    }

    bool is_valid(void* buffer) const
    {
        return std::find(allocations_.begin(), allocations_.end(), buffer) != allocations_.end();
    }

    static void* n_alloc(wy_allocator* self, wy_uword len)
    {
        auto& thiz = get_self(self);
        if (thiz.locked_) { return WY_NULL; }
        auto buf = std::malloc(len);
        thiz.allocations_.push_back(buf);
        return buf;
    }

    static void* n_realloc(wy_allocator* self, void* buffer, wy_uword len)
    {
        auto& thiz = get_self(self);

        if (thiz.locked_) { return WY_NULL; }

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

    static void n_free(wy_allocator* self, void* buffer)
    {
        auto& thiz = get_self(self);
        if (buffer) {
            if (!thiz.is_valid(buffer)) { throw test_allocator_failure("double free detected"); }
            thiz.allocations_.remove(buffer);
            thiz.must_free_.remove(buffer);
            std::free(buffer);
        }
    }

    static wy_uword heap_estimate(wy_allocator* self)
    {
        auto& thiz = get_self(self);
        return thiz.allocations_.size() * 32;
    }

    static inline wy_allocator_vt vt = {
        .alloc = n_alloc,
        .realloc = n_realloc,
        .free = n_free,
        .estimate_heap_size = heap_estimate
    };

    std::list<void*> allocations_;
    std::list<void*> must_free_;
    bool locked_ = false;
};

#endif
