#include <doctest/doctest.h>

#include <fstream>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/fiber.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/module.h>
#include <wyrm/platform/hosted/expand_native.h>
#include <std/io_native.h>
#include "../embed/builtins.h"
#include <wyrm/string.h>
#include <test_common/test_context_fixture.h>

/**
 * Golden corpus runner (design_c_vm.md §9b): loads a fixture's .wyd - built
 * from test/corpus by this build tree's own compiler (scripts/build_fixtures.py,
 * WY_TEST_FIXTURE_DIR), never committed - links it against the builtins
 * module, runs its init through the full dispatch loop, and compares the
 * captured output hook byte-for-byte against the corpus's committed `.out`
 * (WY_TEST_CORPUS_DIR). This exercises the VM's C API and GC directly; the
 * same corpus is checked end to end through the binary by run_behavior.py.
 * Sources the self-hosted compiler cannot yet compile (DIVERGES in
 * test/corpus/manifest.txt) have no fixture and no case here.
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

wy_error fixture_import(wy_context* ctx, const char* path, wy_uword len,
    wy_u8** out, wy_uword* out_len, const wy_module_image** out_image, void* ud)
{
    auto& root = *static_cast<std::string*>(ud);
    std::string relative(path, len);
    for (std::size_t pos = 0; (pos = relative.find("::", pos)) != std::string::npos;) {
        relative.replace(pos, 2, "/");
    }
    std::string file = root + "/" + relative + ".wyd";
    if (!std::ifstream(file, std::ios::binary).good()) {
        /* Not a fixture: the compiled-in library modules (std::io, ...),
         * exactly as the CLI's import hook falls back to its builtin table. */
        for (wy_uword i = 0; i < wyrm_builtin_module_count; i++) {
            if (std::strlen(wyrm_builtin_modules[i].path) == len
                && std::memcmp(wyrm_builtin_modules[i].path, path, len) == 0) {
                *out_image = wyrm_builtin_modules[i].image;
                return WY_ERR_NONE;
            }
        }
        return WY_ERR_UNBOUND;
    }
    auto storage = read_binary_file(file);
    *out = static_cast<wy_u8*>(wy_context_gc_alloc(ctx, storage.size()));
    if (*out == nullptr) { return WY_ERR_NOMEM; }
    std::memcpy(*out, storage.data(), storage.size());
    *out_len = storage.size();
    return WY_ERR_NONE;
}

/**
 * Seed a host-supplied global (`__name__`/`__ARGS`) into a module's free
 * slot before its init runs, mirroring `src/wyrm/main.c`'s `seed_global` -
 * a fixture compiled expecting the real CLI's environment (e.g. samples
 * that read `__ARGS`) needs the same seams here. A no-op for any module
 * that never declared the free name.
 */
wy_error seed_global(wy_context* context, wy_module* module, const char* name, wy_value value)
{
    wy_symbol sym = WY_NULL;
    wy_error err = wy_context_intern(context, name, std::strlen(name), &sym);
    if (err != WY_ERR_NONE) { return err; }
    wy_uword slot = wy_slot_dict_get(&module->free_names, sym);
    if (slot == WY_SLOT_INVALID) { return WY_ERR_NONE; }
    module->globals[slot] = value;
    if (module->fill_layer != WY_NULL) { module->fill_layer[slot] &= WY_LINK_LAYER_MASK; }
    return WY_ERR_NONE;
}

/**
 * Load, link and run `name`.wyd's init, capturing its output. `gc_threshold`
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
    std::string import_root = std::string(WY_TEST_FIXTURE_DIR) + "/" + name.substr(0, name.find_last_of('/'));
    context->import_hook = fixture_import;
    context->import_ud = &import_root;

    wy_module* builtins = WY_NULL;
    REQUIRE_EQ(wy_builtins_new(context, &builtins), WY_ERR_NONE);
    context->builtins = builtins;
    REQUIRE_EQ(wy_io_natives_install(context), WY_ERR_NONE);
    REQUIRE_EQ(wy_expand_module_install(context), WY_ERR_NONE);

    std::vector<wy_u8> storage = read_binary_file(std::string(WY_TEST_FIXTURE_DIR) + "/" + name + ".wyd");
    wy_module* module = WY_NULL;
    REQUIRE_EQ(wy_module_load_bytes(context, storage.data(), storage.size(), false, &module), WY_ERR_NONE);
    REQUIRE_EQ(wy_context_set_root(context, module), WY_ERR_NONE);

    REQUIRE_EQ(wy_link_fill_from_builtins(context, module, builtins), WY_ERR_NONE);

    {
        wy_string* main_name = WY_NULL;
        REQUIRE_EQ(wy_string_new(context, "__main__", 8, &main_name), WY_ERR_NONE);
        REQUIRE_EQ(seed_global(context, module, "__name__", wy_value_object(WY_TYPE_TAG_STR, (wy_object*) main_name)),
            WY_ERR_NONE);
    }
    {
        wy_list* args = WY_NULL;
        REQUIRE_EQ(wy_list_new(context, 0, &args), WY_ERR_NONE);
        if (name.rfind("expand/", 0) == 0) {
            /* expand fixtures locate their sibling images through __ARGS[0]. */
            wy_string* dir = WY_NULL;
            REQUIRE_EQ(wy_string_new(context, import_root.data(), import_root.size(), &dir), WY_ERR_NONE);
            REQUIRE_EQ(wy_list_push(context, args, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) dir)), WY_ERR_NONE);
        }
        REQUIRE_EQ(seed_global(context, module, "__ARGS", wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) args)),
            WY_ERR_NONE);
    }

    wy_error result = wy_module_run_init(context, module);
    if (result != WY_ERR_NONE) {
        INFO("fixture ", name, " faulted: error ", (int) result);
        REQUIRE_EQ(result, WY_ERR_NONE);
    }

    if (name.rfind("expand/", 0) == 0) {
        /* Every expansion VM must be freed completely. */
        CHECK_EQ(wy_expand_leaked_bytes(), 0u);
    }
    return captured;
}

void check_fixture(const std::string& name)
{
    std::string expected = read_text_file(std::string(WY_TEST_CORPUS_DIR) + "/" + name + ".out");
    CHECK_EQ(run_fixture(name, WY_CONTEXT_GC_THRESHOLD_DEFAULT), expected);
}

void check_fixture_gcstress(const std::string& name)
{
    std::string expected = read_text_file(std::string(WY_TEST_CORPUS_DIR) + "/" + name + ".out");
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
    TEST_CASE("classes") { check_fixture("classes"); }
    TEST_CASE("messages") { check_fixture("messages"); }
    TEST_CASE("coroutines") { check_fixture("coroutines"); }
    TEST_CASE("two_module report") { check_fixture("two_module/report"); }
    TEST_CASE("two_module geometry") { check_fixture("two_module/geometry"); }
    TEST_CASE("two_module shapes") { check_fixture("two_module/shapes"); }
    TEST_CASE("two_module shapes_main") { check_fixture("two_module/shapes_main"); }
    TEST_CASE("two_module dunder_name") { check_fixture("two_module/dunder_name"); }
    TEST_CASE("two_module dunder_name_main") { check_fixture("two_module/dunder_name_main"); }
    TEST_CASE("range") { check_fixture("range"); }
    TEST_CASE("template") { check_fixture("template"); }
    TEST_CASE("template gcstress") { check_fixture_gcstress("template"); }
    TEST_CASE("expand treemain") { check_fixture("expand/treemain"); }
    TEST_CASE("expand expandmain") { check_fixture("expand/expandmain"); }
    TEST_CASE("wildcard paint") { check_fixture("wildcard/paint"); }
    TEST_CASE("wildcard palette") { check_fixture("wildcard/palette"); }
    TEST_CASE("samples/eval_args") { check_fixture("samples/eval_args"); }
    /* Every DIVERGES row of test/corpus/manifest.txt is absent: no fixture
     * is built for it. Add its case here when the row flips to matches. */
    TEST_CASE("samples/eval_assignments") { check_fixture("samples/eval_assignments"); }
    TEST_CASE("samples/eval_closures") { check_fixture("samples/eval_closures"); }
    TEST_CASE("samples/eval_control_flow") { check_fixture("samples/eval_control_flow"); }
    TEST_CASE("samples/eval_functions") { check_fixture("samples/eval_functions"); }
    TEST_CASE("samples/eval_range") { check_fixture("samples/eval_range"); }
}

TEST_SUITE("golden-gcstress")
{
    TEST_CASE("range") { check_fixture_gcstress("range"); }
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
    TEST_CASE("classes") { check_fixture_gcstress("classes"); }
    TEST_CASE("messages") { check_fixture_gcstress("messages"); }
    TEST_CASE("coroutines") { check_fixture_gcstress("coroutines"); }
    TEST_CASE("two_module report") { check_fixture_gcstress("two_module/report"); }
    TEST_CASE("two_module geometry") { check_fixture_gcstress("two_module/geometry"); }
    TEST_CASE("two_module shapes") { check_fixture_gcstress("two_module/shapes"); }
    TEST_CASE("two_module shapes_main") { check_fixture_gcstress("two_module/shapes_main"); }
    TEST_CASE("two_module dunder_name") { check_fixture_gcstress("two_module/dunder_name"); }
    TEST_CASE("two_module dunder_name_main") { check_fixture_gcstress("two_module/dunder_name_main"); }
    TEST_CASE("expand treemain") { check_fixture_gcstress("expand/treemain"); }
    TEST_CASE("expand expandmain") { check_fixture_gcstress("expand/expandmain"); }
    TEST_CASE("wildcard paint") { check_fixture_gcstress("wildcard/paint"); }
    TEST_CASE("wildcard palette") { check_fixture_gcstress("wildcard/palette"); }
    TEST_CASE("samples/eval_args") { check_fixture_gcstress("samples/eval_args"); }
    TEST_CASE("samples/eval_assignments") { check_fixture_gcstress("samples/eval_assignments"); }
    TEST_CASE("samples/eval_closures") { check_fixture_gcstress("samples/eval_closures"); }
    TEST_CASE("samples/eval_control_flow") { check_fixture_gcstress("samples/eval_control_flow"); }
    TEST_CASE("samples/eval_functions") { check_fixture_gcstress("samples/eval_functions"); }
    TEST_CASE("samples/eval_range") { check_fixture_gcstress("samples/eval_range"); }
}
