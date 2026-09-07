#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/sys/string.h>

TEST_CASE("Implementation is CWYRM") {
    CHECK(wy_strcmp_f(wy_lib_implementation(), "cwyrm") == 0);
}
