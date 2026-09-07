#include <doctest/doctest.h>

#include <wyrm/platform/glib/mainloop.h>
#include <wyrm/platform/hosted/allocator_cmem.h>

#include <wyrmxx/main_loop.h>
#include <wyrmxx/platform/glib/mainloop.h>

#include "test_common/test_main_loop.h"
#include "test_common/test_allocator_fixture.h"

TEST_SUITE("platform_glib_main_loop")
{
    TEST_CASE("init/deinit") {
        test_allocator_fixture ta;
        wyrmxx::glib_mainloop loop{ ta };
        loop.release();
        ta.check();
    }


    TEST_CASE("wakeable source can be triggered") {
        test_allocator_fixture ta;

        wy_main_loop *loop = wy_glib_mainloop_new(ta.ptr());
        REQUIRE(loop != nullptr);

        wy_test_main_loop_wakeable_sanity(loop);
        CHECK(wy_glib_mainloop_get_active_sources(loop) == 0);

        CHECK(wy_glib_mainloop_destroy(loop) == WY_ERR_NONE);
        ta.check();
    }

    TEST_CASE("timer fires within short delay") {
        test_allocator_fixture ta;

        wy_main_loop *loop = wy_glib_mainloop_new(ta.ptr());
        REQUIRE(loop != nullptr);

        wy_test_main_loop_timer_sanity(loop, 20);
        CHECK(wy_glib_mainloop_get_active_sources(loop) == 0);

        CHECK(wy_glib_mainloop_destroy(loop) == WY_ERR_NONE);
        ta.check();
    }
}
