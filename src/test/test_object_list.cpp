#include <doctest/doctest.h>
#include <wyrm/api.h>
#include "test_common/test_allocator_fixture.h"

TEST_SUITE("object_list")
{
    TEST_CASE("static initialization can push") {
        wyrm_object_list obj_list{};
        wyrm_object sentinel{};
        wyrm_object* arr[2] = {};

        wyrm_object_list_init_s(&obj_list, nullptr, arr, 2);
        wyrm_object_list_push(&obj_list, &sentinel);
        CHECK_EQ(wyrm_object_list_idx_f(&obj_list, 0), &sentinel);
    }

    TEST_CASE("dynamic allocation works")
    {
        wyrm_object_list obj_list{};
        test_allocator_fixture fix;
        wyrm_object sentinel{};

        wyrm_object_list_init_f(&obj_list, fix.ptr());
        REQUIRE_EQ(wyrm_object_list_push(&obj_list, &sentinel), WYRM_ERR_NONE);
        CHECK_EQ(wyrm_object_list_idx_f(&obj_list, 0), &sentinel);
    }
}
