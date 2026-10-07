#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <filesystem>
#include <wyrm.h>
#include <wyrm/image_loader.h>
#include <wyrm/module.h>
#include <wyrm/string.h>
#include <test_common/test_context_fixture.h>

// hello_1.c, emitted by this build's compiler from test/corpus/hello_1.wy
// and compiled straight into test_cwyrm (see src/test/meson.build).
extern "C" {
extern const wy_module_image hello_1_image;
}

namespace
{

std::vector<wy_u8> read_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "could not open ", path);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    return std::vector<wy_u8>(s.begin(), s.end());
}

/**
 * Load a fixture's .wyd. `storage` must outlive the returned module: the
 * loader never copies `code` (or the debug section, unread here), so
 * wy_module::code points straight into it.
 */
wy_module* load_fixture(wy_context* context, const std::string& rel_path, std::vector<wy_u8>& storage)
{
    storage = read_file(std::string(WY_TEST_FIXTURE_DIR) + "/" + rel_path);
    wy_module* module = nullptr;
    REQUIRE_EQ(wy_module_load_bytes(context, storage.data(), storage.size(), false, &module), WY_ERR_NONE);
    return module;
}

wy_symbol intern(wy_context* context, const char* text)
{
    wy_symbol sym;
    REQUIRE_EQ(wy_context_intern(context, text, wy_strlen_f(text), &sym), WY_ERR_NONE);
    return sym;
}

}  // namespace

TEST_SUITE("module")
{
    TEST_CASE("hello.wyd loads its header, statics, functions, exports and free tables")
    {
        test_context_fixture ctx;
        std::vector<wy_u8> storage;
        wy_module* module = load_fixture(ctx.get_context_ptr(), "hello.wyd", storage);

        CHECK_EQ(module->global_count, 2);
        CHECK_EQ(module->init_nlocals, 3);

        REQUIRE_EQ(module->static_count, 2);
        REQUIRE_EQ(module->statics[0].type, WY_TYPE_TAG_STR);
        CHECK_EQ(wy_strncmp_f(module->statics[0].data.str->str, "Hello ", 6), 0);
        REQUIRE_EQ(module->statics[1].type, WY_TYPE_TAG_STR);
        CHECK_EQ(wy_strncmp_f(module->statics[1].data.str->str, "World", 5), 0);

        REQUIRE_EQ(module->function_count, 1);
        const wy_function_proto& greet = module->functions[0];
        CHECK_EQ(wy_strcmp_f(greet.name, "greet"), 0);
        CHECK_EQ(greet.nparams, 1);
        CHECK_EQ(greet.nlocals, 2);
        CHECK_EQ(greet.code_offset, 11);
        CHECK_EQ(greet.flags, 0);

        CHECK_EQ(wy_slot_dict_get(&module->exports, intern(ctx.get_context_ptr(), "greet")), 0);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx.get_context_ptr(), "println")), 1);
    }

    TEST_CASE("classes.wyd loads slot layout and message maps")
    {
        test_context_fixture ctx;
        std::vector<wy_u8> storage;
        wy_module* module = load_fixture(ctx.get_context_ptr(), "classes.wyd", storage);

        REQUIRE_EQ(module->class_count, 2);

        const wy_class_proto& shape = module->class_protos[0];
        CHECK_EQ(wy_strcmp_f(shape.name, "Shape"), 0);
        CHECK_EQ(shape.super_slot, -1);
        CHECK_EQ(shape.init_fn, 0);
        REQUIRE_EQ(shape.nslots, 2);
        CHECK_EQ(wy_strcmp_f(shape.slots[0].name, "name"), 0);
        CHECK_EQ(shape.slots[0].default_static, 0);
        CHECK_EQ(wy_strcmp_f(shape.slots[1].name, "sides"), 0);
        CHECK_EQ(shape.slots[1].default_static, 1);
        REQUIRE_EQ(shape.nmsgs, 2);
        CHECK_EQ(wy_strcmp_f(shape.msgs[0].name, "describe"), 0);
        CHECK_EQ(shape.msgs[0].fn, 1);
        CHECK_EQ(wy_strcmp_f(shape.msgs[1].name, "corners"), 0);
        CHECK_EQ(shape.msgs[1].fn, 2);
        CHECK_EQ(shape.nstatics, 0);

        const wy_class_proto& square = module->class_protos[1];
        CHECK_EQ(wy_strcmp_f(square.name, "Square"), 0);
        CHECK_EQ(square.super_slot, 0);  // Shape's global slot
        CHECK_EQ(square.init_fn, 3);
        REQUIRE_EQ(square.nslots, 1);
        CHECK_EQ(wy_strcmp_f(square.slots[0].name, "size"), 0);
        CHECK_EQ(square.slots[0].default_static, 2);
        REQUIRE_EQ(square.nmsgs, 1);
        CHECK_EQ(wy_strcmp_f(square.msgs[0].name, "area"), 0);
        CHECK_EQ(square.msgs[0].fn, 4);
        CHECK_EQ(square.nstatics, 0);

        REQUIRE_EQ(module->message_count, 3);
        REQUIRE_EQ(module->messages[0].path_len, 1);
        CHECK_EQ(module->messages[0].path[0], 2);  // "describe"
        REQUIRE_EQ(module->messages[1].path_len, 1);
        CHECK_EQ(module->messages[1].path[0], 3);  // "corners"
        REQUIRE_EQ(module->messages[2].path_len, 1);
        CHECK_EQ(module->messages[2].path[0], 5);  // "area"
    }

    TEST_CASE("two_module/report.wyd loads its messages (none) and free table")
    {
        test_context_fixture ctx;
        std::vector<wy_u8> storage;
        wy_module* module = load_fixture(ctx.get_context_ptr(), "two_module/report.wyd", storage);

        CHECK_EQ(module->message_count, 0);

        CHECK_EQ(wy_slot_dict_get(&module->exports, intern(ctx.get_context_ptr(), "geometry")), 0);
        CHECK_EQ(wy_slot_dict_get(&module->exports, intern(ctx.get_context_ptr(), "area")), 1);
        CHECK_EQ(wy_slot_dict_get(&module->exports, intern(ctx.get_context_ptr(), "pretty")), 2);
        CHECK_EQ(wy_slot_dict_get(&module->exports, intern(ctx.get_context_ptr(), "summary")), 3);

        // An item import is its member's free slot, exported under the name
        // it binds (design/modules.md M2): `area` and `pretty` *are*
        // geometry's `area` and `label`.
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx.get_context_ptr(), "geometry::area")), 1);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx.get_context_ptr(), "geometry::label")), 2);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx.get_context_ptr(), "println")), 4);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx.get_context_ptr(), "geometry::UNITS")), 5);
    }

    TEST_CASE("an embedded .c image loads through wy_module_load_image")
    {
        test_context_fixture ctx;
        wy_module* module = nullptr;
        REQUIRE_EQ(wy_module_load_image(ctx.get_context_ptr(), &hello_1_image, &module), WY_ERR_NONE);

        CHECK_EQ(module->global_count, 1);
        CHECK_EQ(module->init_nlocals, 2);
        REQUIRE_EQ(module->static_count, 1);
        REQUIRE_EQ(module->statics[0].type, WY_TYPE_TAG_STR);
        CHECK_EQ(wy_strncmp_f(module->statics[0].data.str->str, "Hello World\n", 12), 0);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx.get_context_ptr(), "print")), 0);
    }
}

TEST_SUITE("loader")
{
    TEST_CASE("every built fixture loads")
    {
        // Every .wyd build_fixtures.py produced (the manifest's runnable rows
        // plus expand/ and embedded/) must load.
        wy_uword checked = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(WY_TEST_FIXTURE_DIR)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".wyd") { continue; }
            INFO("fixture: ", entry.path().string());
            std::vector<wy_u8> bytes = read_file(entry.path().string());

            test_context_fixture ctx;
            wy_module* module = nullptr;
            CHECK_EQ(wy_module_load_bytes(ctx.get_context_ptr(), bytes.data(), bytes.size(), false, &module), WY_ERR_NONE);
            checked++;
        }

        CHECK_GT(checked, 20u);
    }
}
