#include <doctest/doctest.h>

#include <wyrm.h>
#include <test_common/test_context_fixture.h>

TEST_SUITE("wstring") {
    TEST_CASE("init sets allocator and null context") {
        test_context_fixture fix;

        wyrm_string* str_ptr = WYRM_NULL;
        REQUIRE_EQ(wyrm_string_strdup(fix.get_context_ptr(), "Hello World", &str_ptr), WYRM_ERR_NONE);
        REQUIRE_NE(str_ptr, WYRM_NULL);

        fix.allocator.watch(str_ptr->str); str_ptr = WYRM_NULL;
        fix.run_gc();
        fix.allocator.check_watched_free();
    }

    TEST_CASE("hash different for common keywords") {
        test_context_fixture fix;

        wyrm_string* str1 = WYRM_NULL;
        REQUIRE_EQ(wyrm_string_strdup(fix.get_context_ptr(), "if", &str1), WYRM_ERR_NONE);
        REQUIRE_NE(str1, WYRM_NULL);

        wyrm_string* str2 = WYRM_NULL;
        REQUIRE_EQ(wyrm_string_strdup(fix.get_context_ptr(), "fn", &str2), WYRM_ERR_NONE);
        REQUIRE_NE(str2, WYRM_NULL);

        REQUIRE_NE(str1->hash, str2->hash);
    }

    TEST_CASE("prefix match not equal") {
        test_context_fixture fix;

        wyrm_string* str1 = WYRM_NULL;
        REQUIRE_EQ(wyrm_string_new(fix.get_context_ptr(), "if2", 2, &str1), WYRM_ERR_NONE);
        REQUIRE_NE(str1, WYRM_NULL);

        wyrm_string* str2 = WYRM_NULL;
        REQUIRE_EQ(wyrm_string_new(fix.get_context_ptr(), "if2", 3, &str2), WYRM_ERR_NONE);
        REQUIRE_NE(str2, WYRM_NULL);

        REQUIRE_NE(str1->hash, str2->hash);
    }

    TEST_CASE("oom handled") {
        test_context_fixture fix;

        wyrm_string sentinel{};
        wyrm_string* str_ptr = &sentinel;
        fix.allocator.set_locked(true);
        REQUIRE_EQ(wyrm_string_strdup(fix.get_context_ptr(), "Hello World", &str_ptr), WYRM_ERR_NOMEM);
    }
}
