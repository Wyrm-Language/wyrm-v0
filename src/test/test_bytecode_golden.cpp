#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/fiber.h>
#include <wyrm/link.h>
#include <wyrm/module.h>
#include <test_common/test_context_fixture.h>

/**
 * Golden corpus runner (design_c_vm.md §9b): loads a fixture's .wyc, links
 * it against the builtins module, runs its init through the full dispatch
 * loop, and compares the captured output hook byte-for-byte against the
 * corpus's committed `.out`. One TEST_CASE per fixture in the epic 2 M5
 * target set (hello, hello_1/2/3, arith, control_flow, multiret) plus the
 * epic 3 M1/M2 fixtures (closures, collections) - every other manifest row
 * is out of scope until a later epic exercises it.
 */

namespace {

std::vector<wy_u8> read_binary_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "could not open ", path);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    return std::vector<wy_u8>(s.begin(), s.end());
}

std::string read_text_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "could not open ", path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void capture_write_(wy_context*, const char* bytes, wy_uword len, void* ud)
{
    static_cast<std::string*>(ud)->append(bytes, len);
}

/**
 * Load, link and run `name`.wyc's init, capturing its output. `gc_threshold`
 * lets the -gcstress variant collect at every safepoint instead of the
 * default pressure threshold (design_c_vm.md §8's stress mode, for finding
 * a missing children_iter entry).
 */
std::string run_fixture(const std::string& name, wy_uword gc_threshold)
{
    test_context_fixture fix;
    wy_context* context = fix.get_context_ptr();

    wy_fiber* fiber = wy_fiber_create(context, 4096, 256);
    REQUIRE_NE(fiber, WY_NULL);
    REQUIRE_EQ(wy_context_attach_fiber(context, fiber), WY_ERR_NONE);

    std::string captured;
    context->io.write = capture_write_;
    context->io.ud = &captured;
    context->gc_threshold = gc_threshold;

    wy_module* builtins = WY_NULL;
    REQUIRE_EQ(wy_builtins_new(context, &builtins), WY_ERR_NONE);
    context->builtins = builtins;

    std::vector<wy_u8> storage = read_binary_file(std::string(WY_TEST_BYTECODE_DIR) + "/" + name + ".wyc");
    wy_module* module = WY_NULL;
    REQUIRE_EQ(wy_module_load_bytes(context, storage.data(), storage.size(), false, &module), WY_ERR_NONE);
    REQUIRE_EQ(wy_context_set_root(context, module), WY_ERR_NONE);

    REQUIRE_EQ(wy_link_fill_from_builtins(context, module, builtins), WY_ERR_NONE);

    wy_error result = wy_module_run_init(context, module);
    if (result != WY_ERR_NONE) {
        INFO("fixture ", name, " faulted: error ", (int) result);
        REQUIRE_EQ(result, WY_ERR_NONE);
    }

    return captured;
}

void check_fixture(const std::string& name)
{
    std::string expected = read_text_file(std::string(WY_TEST_BYTECODE_DIR) + "/" + name + ".out");
    CHECK_EQ(run_fixture(name, WY_CONTEXT_GC_THRESHOLD_DEFAULT), expected);
}

void check_fixture_gcstress(const std::string& name)
{
    std::string expected = read_text_file(std::string(WY_TEST_BYTECODE_DIR) + "/" + name + ".out");
    CHECK_EQ(run_fixture(name, 0), expected);
}

} // namespace

TEST_SUITE("golden")
{
    TEST_CASE("hello") { check_fixture("hello"); }
    TEST_CASE("hello_1") { check_fixture("hello_1"); }
    TEST_CASE("hello_2") { check_fixture("hello_2"); }
    TEST_CASE("hello_3") { check_fixture("hello_3"); }
    TEST_CASE("arith") { check_fixture("arith"); }
    TEST_CASE("control_flow") { check_fixture("control_flow"); }
    TEST_CASE("multiret") { check_fixture("multiret"); }
    TEST_CASE("closures") { check_fixture("closures"); }
    TEST_CASE("collections") { check_fixture("collections"); }
    TEST_CASE("errors") { check_fixture("errors"); }
}

TEST_SUITE("golden-gcstress")
{
    TEST_CASE("hello") { check_fixture_gcstress("hello"); }
    TEST_CASE("hello_1") { check_fixture_gcstress("hello_1"); }
    TEST_CASE("hello_2") { check_fixture_gcstress("hello_2"); }
    TEST_CASE("hello_3") { check_fixture_gcstress("hello_3"); }
    TEST_CASE("arith") { check_fixture_gcstress("arith"); }
    TEST_CASE("control_flow") { check_fixture_gcstress("control_flow"); }
    TEST_CASE("multiret") { check_fixture_gcstress("multiret"); }
    TEST_CASE("closures") { check_fixture_gcstress("closures"); }
    TEST_CASE("collections") { check_fixture_gcstress("collections"); }
    TEST_CASE("errors") { check_fixture_gcstress("errors"); }
}
