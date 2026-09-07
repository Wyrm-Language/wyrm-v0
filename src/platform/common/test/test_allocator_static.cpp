#include <doctest/doctest.h>
#include <wyrm/platform/common/allocator_static.h>
#include <wyrm.h>

TEST_SUITE("allocator_static")
{
    TEST_CASE("wy_allocator_static vtable set after init") {
        wy_allocator_static a = {};
        char buffer[128];
        wy_allocator_static_init(&a, buffer, sizeof(buffer));
        CHECK((a.base.clz == &wy_allocator_static_vt));
        CHECK((wy_allocator_from_static(&a) == &a.base));
    }

    TEST_CASE("wy_allocator_static alloc zero returns null") {
        wy_allocator_static a = {};
        char buffer[128];
        wy_allocator_static_init(&a, buffer, sizeof(buffer));
        wy_allocator* alloc = wy_allocator_from_static(&a);
        CHECK((wy_allocator_alloc(alloc, 0) == nullptr));
    }

    TEST_CASE("wy_allocator_static alloc and free with stats") {
        wy_allocator_static a = {};
        char buffer[512];
        wy_allocator_static_init(&a, buffer, sizeof(buffer));
        wy_allocator* alloc = wy_allocator_from_static(&a);

        void* p = wy_allocator_alloc(alloc, 64);
        REQUIRE((p != nullptr));
        static_cast<unsigned char*>(p)[0] = 0xAB;
        auto heap_estimate = wy_allocator_estimate_heap_size(alloc);
        REQUIRE_GT(heap_estimate, 64);

        wy_allocator_free(alloc, p);
    }

    TEST_CASE("wy_allocator_static realloc grows allocation") {
        wy_allocator_static a = {};
        char buffer[512];
        wy_allocator_static_init(&a, buffer, sizeof(buffer));
        wy_allocator* alloc = wy_allocator_from_static(&a);

        void* p = wy_allocator_alloc(alloc, 64);
        REQUIRE((p != nullptr));
        static_cast<unsigned char*>(p)[0] = 0x01;

        void* p2 = wy_allocator_realloc(alloc, p, 128);
        REQUIRE(p2 != nullptr);
        CHECK((static_cast<unsigned char*>(p2)[0] == 0x01));
        wy_allocator_free(alloc, p2);
    }

    TEST_CASE("wy_allocator_static realloc does not copy past original size") {
        wy_allocator_static a = {};
        alignas(8) char buffer[512] = {};
        wy_allocator_static_init(&a, buffer, sizeof(buffer));
        wy_allocator* alloc = wy_allocator_from_static(&a);

        // Alloc 32 bytes and fill with 0xAA
        auto* p1 = static_cast<unsigned char*>(wy_allocator_alloc(alloc, 32));
        REQUIRE(p1 != nullptr);
        for (int i = 0; i < 32; ++i) { p1[i] = 0xAA; }

        // Alloc another 32 bytes adjacent to p1 in the arena and fill with 0xBB
        auto* p2 = static_cast<unsigned char*>(wy_allocator_alloc(alloc, 32));
        REQUIRE(p2 != nullptr);
        for (int i = 0; i < 32; ++i) { p2[i] = 0xBB; }

        // Realloc p1 to 64 bytes — must copy only 32 bytes, not read into p2's memory
        auto* p3 = static_cast<unsigned char*>(wy_allocator_realloc(alloc, p1, 64));
        REQUIRE(p3 != nullptr);

        for (int i = 0;  i < 32; ++i) { CHECK(p3[i] == 0xAA); }
        // Bytes 32-63 must be zero (fresh arena), not 0xBB leaked from p2
        for (int i = 32; i < 64; ++i) { CHECK(p3[i] == 0x00); }
    }
}
