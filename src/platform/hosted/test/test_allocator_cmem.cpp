#include <doctest/doctest.h>
#include <wyrm/platform/hosted/allocator_cmem.h>

TEST_SUITE("allocator_cmem")
{
    TEST_CASE("wy_allocator_cmem vtable set after init") {
        wy_allocator_cmem a;
        wy_allocator_cmem_init(&a);
        CHECK(a.base.clz == &wy_allocator_cmem_vt);
    }

    TEST_CASE("wy_allocator_cmem from_cmem returns base pointer") {
        wy_allocator_cmem a;
        wy_allocator_cmem_init(&a);
        CHECK(wy_allocator_from_cmem(&a) == &a.base);
    }

    TEST_CASE("wy_allocator_cmem alloc zero returns null") {
        wy_allocator_cmem a = {};
        wy_allocator_cmem_init(&a);
        wy_allocator* alloc = wy_allocator_from_cmem(&a);
        CHECK((wy_allocator_alloc(alloc, 0) == nullptr));
    }

    TEST_CASE("wy_allocator_cmem alloc and free with stats") {
        wy_allocator_cmem a = {};
        wy_allocator_cmem_init(&a);
        wy_allocator* alloc = wy_allocator_from_cmem(&a);

        void* p = wy_allocator_alloc(alloc, 64);
        REQUIRE((p != nullptr));
        static_cast<unsigned char*>(p)[0] = 0xAB;
        auto heap_estimate = wy_allocator_estimate_heap_size(alloc);
        REQUIRE_GT(heap_estimate, 64);

        wy_allocator_free(alloc, p);
        REQUIRE_LT(wy_allocator_estimate_heap_size(alloc), heap_estimate);
    }

    TEST_CASE("wy_allocator_cmem realloc grows allocation") {
        wy_allocator_cmem a;
        wy_allocator_cmem_init(&a);
        wy_allocator* alloc = wy_allocator_from_cmem(&a);

        void* p = wy_allocator_alloc(alloc, 64);
        REQUIRE((p != nullptr));
        static_cast<unsigned char*>(p)[0] = 0x01;

        void* p2 = wy_allocator_realloc(alloc, p, 128);
        REQUIRE(p2 != nullptr);
        CHECK((static_cast<unsigned char*>(p2)[0] == 0x01));
        wy_allocator_free(alloc, p2);
    }
}
