#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/sys/string.h>

TEST_CASE("Implementation is CWYRM") {
    CHECK(wyrm_strcmp_f(wyrm_lib_implementation(), "cwyrm") == 0);
}
