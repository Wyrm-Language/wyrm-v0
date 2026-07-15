#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wstring") {
    TEST_CASE("init sets allocator and null context") {
        test_context_fixture fix;

        wyrm_string* str_ptr = WYRM_NULL;
        REQUIRE_EQ(wyrm_string_strdup(fix.get_context_ptr(), "Hello World", &str_ptr), WYRM_ERR_NONE);
        REQUIRE_NE(str_ptr, WYRM_NULL);

        fix.allocator.watch(str_ptr->str);
        wyrm_string_finalize_f(fix.get_context_ptr(), str_ptr);
        fix.allocator.check_watched_free();
    }

    TEST_CASE("oom handled") {
        test_context_fixture fix;

        wyrm_string sentinel{};
        wyrm_string* str_ptr = &sentinel;
        fix.allocator.set_locked(true);
        REQUIRE_EQ(wyrm_string_strdup(fix.get_context_ptr(), "Hello World", &str_ptr), WYRM_ERR_NOMEM);
    }
}
