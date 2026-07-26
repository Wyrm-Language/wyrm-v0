#include <doctest/doctest.h>
#include <wyrm/platform/hosted/allocator_cmem.h>

TEST_SUITE("allocator_cmem")
{
    TEST_CASE("wyrm_allocator_cmem vtable set after init") {
        wyrm_allocator_cmem a;
        wyrm_allocator_cmem_init(&a);
        CHECK(a.base.clz == &wyrm_allocator_cmem_vt);
    }

    TEST_CASE("wyrm_allocator_cmem from_cmem returns base pointer") {
        wyrm_allocator_cmem a;
        wyrm_allocator_cmem_init(&a);
        CHECK(wyrm_allocator_from_cmem(&a) == &a.base);
    }

    TEST_CASE("wyrm_allocator_cmem alloc zero returns null") {
        wyrm_allocator_cmem a = {};
        wyrm_allocator_cmem_init(&a);
        wyrm_allocator* alloc = wyrm_allocator_from_cmem(&a);
        CHECK((wyrm_allocator_alloc(alloc, 0) == nullptr));
    }

    TEST_CASE("wyrm_allocator_cmem alloc and free with stats") {
        wyrm_allocator_cmem a = {};
        wyrm_allocator_cmem_init(&a);
        wyrm_allocator* alloc = wyrm_allocator_from_cmem(&a);

        void* p = wyrm_allocator_alloc(alloc, 64);
        REQUIRE((p != nullptr));
        static_cast<unsigned char*>(p)[0] = 0xAB;
        auto heap_estimate = wyrm_allocator_estimate_heap_size(alloc);
        REQUIRE_GT(heap_estimate, 64);

        wyrm_allocator_free(alloc, p);
        REQUIRE_LT(wyrm_allocator_estimate_heap_size(alloc), heap_estimate);
    }

    TEST_CASE("wyrm_allocator_cmem realloc grows allocation") {
        wyrm_allocator_cmem a;
        wyrm_allocator_cmem_init(&a);
        wyrm_allocator* alloc = wyrm_allocator_from_cmem(&a);

        void* p = wyrm_allocator_alloc(alloc, 64);
        REQUIRE((p != nullptr));
        static_cast<unsigned char*>(p)[0] = 0x01;

        void* p2 = wyrm_allocator_realloc(alloc, p, 128);
        REQUIRE(p2 != nullptr);
        CHECK((static_cast<unsigned char*>(p2)[0] == 0x01));
        wyrm_allocator_free(alloc, p2);
    }
}
