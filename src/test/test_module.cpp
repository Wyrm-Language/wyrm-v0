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
#include <wyrm/gc_flags.h>
#include <wyrm/opcode.h>
#include <test_common/cbor_build.h>
#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

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
 * loader uses `code` in place when it is aligned (and never reads the debug
 * section), so wy_module::code may point straight into it.
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

using namespace wy_test_cbor;

// A header for a module with `globals` global slots.
Bytes header_with(int globals) { return map({kv("n", text("m")), kv("g", uval(globals)), kv("l", uval(2))}); }

// Load `sections` as a file; the error, or WY_ERR_NONE with `*module` set.
wy_error load_sections(wy_context* context, const std::vector<Section>& sections, wy_module** module)
{
    // A fresh copy for each load: the module keeps pointers into it.
    static std::vector<Bytes> keep;
    keep.push_back(container(sections));
    return wy_module_load_bytes(context, keep.back().data(), keep.back().size(), false, module);
}

wy_error load_sections(wy_context* context, const std::vector<Section>& sections)
{
    wy_module* module = nullptr;
    return load_sections(context, sections, &module);
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
    TEST_CASE("code is used in place when aligned and copied to an aligned buffer when not")
    {
        std::vector<wy_u8> original = read_file(std::string(WY_TEST_FIXTURE_DIR) + "/hello.wyd");
        wy_module_image probe;
        REQUIRE_EQ(wy_image_from_bytes(original.data(), original.size(), &probe), WY_ERR_NONE);
        const wy_uword code_at = (wy_uword) (probe.sections[WY_SEC_CODE].data - original.data());
        const wy_uword code_bytes = probe.sections[WY_SEC_CODE].len;
        REQUIRE_GT(code_bytes, 0u);

        int aligned_cases = 0, copied_cases = 0;
        for (wy_uword shift = 0; shift < 4; shift++) {
            CAPTURE(shift);
            // Slide the file so its code starts at each of the four alignments.
            std::vector<wy_u8> storage(original.size() + 8);
            wy_uword base = (wy_uword) (-(std::intptr_t) (storage.data() + code_at)) & 3;
            wy_u8* file = storage.data() + base + shift;
            std::memcpy(file, original.data(), original.size());

            test_context_fixture ctx;
            wy_module* module = nullptr;
            REQUIRE_EQ(wy_module_load_bytes(ctx.get_context_ptr(), file, original.size(), false, &module), WY_ERR_NONE);
            REQUIRE_EQ(module->code_len, code_bytes / 4);
            CHECK_EQ((std::uintptr_t) module->code % alignof(wy_u32), 0u);
            CHECK_EQ(std::memcmp(module->code, original.data() + code_at, code_bytes), 0);

            bool in_file = (std::uintptr_t) (file + code_at) % alignof(wy_u32) == 0;
            if (in_file) {
                aligned_cases++;
                CHECK_EQ(module->owned_code, nullptr);
                CHECK_EQ((const void*) module->code, (const void*) (file + code_at));
            } else {
                copied_cases++;
                CHECK_NE(module->owned_code, nullptr);
                CHECK_EQ((const void*) module->code, (const void*) module->owned_code);
            }
        }
        CHECK_EQ(aligned_cases, 1);
        CHECK_EQ(copied_cases, 3);
    }

    TEST_CASE("a module with copied code still runs its init")
    {
        // `return` with no values; a header plus this is a complete module.
        const wy_u32 return_word = (wy_u32(WY_OP_RETURN) & 0xFF);
        Bytes original = container({
            {WY_SEC_HEADER, map({kv("n", text("m")), kv("g", uval(0)), kv("l", uval(0))})},
            {WY_SEC_CODE, code_item({return_word})},
        });
        wy_module_image probe;
        REQUIRE_EQ(wy_image_from_bytes(original.data(), original.size(), &probe), WY_ERR_NONE);
        const wy_uword code_at = (wy_uword) (probe.sections[WY_SEC_CODE].data - original.data());
        for (wy_uword shift = 0; shift < 4; shift++) {
            CAPTURE(shift);
            std::vector<wy_u8> storage(original.size() + 8);
            wy_u8* file = storage.data() + shift;
            std::memcpy(file, original.data(), original.size());

            test_fiber_fixture fix;
            wy_context* ctx = fix.get_context_ptr();
            wy_module* module = nullptr;
            REQUIRE_EQ(wy_module_load_bytes(ctx, file, original.size(), false, &module), WY_ERR_NONE);
            bool copied = (std::uintptr_t) (file + code_at) % alignof(wy_u32) != 0;
            CHECK_EQ(module->owned_code != nullptr, copied);
            CHECK_EQ(wy_module_run_init(ctx, module), WY_ERR_NONE);
            CHECK_EQ(module->state, WY_MODULE_READY);
        }
    }

    TEST_CASE("statics decode string, integer, float, bool, nil and bytes")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* module = nullptr;
        REQUIRE_EQ(load_sections(ctx, {
            {WY_SEC_HEADER, header_with(0)},
            {WY_SEC_STATICS, array({
                text("héllo"), uval(0), uval(0x123456789ull), integer(-1), integer(INT64_MIN + 1),
                float64(2.5), boolean(true), boolean(false), nil(), byte_string({0, 255, 1}),
            })},
            {WY_SEC_CODE, code_item({})},
        }, &module), WY_ERR_NONE);
        REQUIRE_EQ(module->static_count, 10u);
        CHECK_EQ(module->statics[0].type, WY_TYPE_TAG_STR);
        CHECK_EQ(std::string(module->statics[0].data.str->str, module->statics[0].data.str->len), "héllo");
        CHECK_EQ(module->statics[0].data.str->object.flags & WY_GC_FLAG_BINARY, 0);
        CHECK_EQ(module->statics[1].data.word, 0);
        CHECK_EQ(module->statics[2].data.word, 0x123456789ll);
        CHECK_EQ(module->statics[3].data.word, -1);
        CHECK_EQ(module->statics[4].data.word, INT64_MIN + 1);
        CHECK_EQ(module->statics[5].type, WY_TYPE_TAG_FLOAT);
        CHECK_EQ(module->statics[5].data.fp, 2.5);
        CHECK_EQ(module->statics[6].type, WY_TYPE_TAG_BOOL);
        CHECK(module->statics[6].data.flag);
        CHECK_FALSE(module->statics[7].data.flag);
        CHECK_EQ(module->statics[8].type, WY_TYPE_TAG_NIL);
        REQUIRE_EQ(module->statics[9].type, WY_TYPE_TAG_STR);
        CHECK_NE(module->statics[9].data.str->object.flags & WY_GC_FLAG_BINARY, 0);
        CHECK_EQ(module->statics[9].data.str->len, 3u);
        CHECK_EQ((wy_u8) module->statics[9].data.str->str[1], 255);
    }

    TEST_CASE("slot_defaults keys are unsigned slot indices")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* module = nullptr;
        REQUIRE_EQ(load_sections(ctx, {
            {WY_SEC_HEADER, header_with(3)},
            {WY_SEC_SLOT_DEFAULTS, map({Pair{uval(2), integer(7)}, Pair{uval(0), text("d")}})},
            {WY_SEC_CODE, code_item({})},
        }, &module), WY_ERR_NONE);
        REQUIRE_EQ(module->global_count, 3u);
        CHECK_EQ(module->globals[2].data.word, 7);
        CHECK_EQ(module->globals[0].type, WY_TYPE_TAG_STR);
        CHECK(wy_value_is_unset(module->globals[1]));

        // A string key, as the BSON writer spelled them, is refused, and so is
        // a slot past the module's globals.
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(3)}, {WY_SEC_SLOT_DEFAULTS, map({kv("2", integer(7))})}, {WY_SEC_CODE, code_item({})}}), WY_ERR_IMAGE);
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(3)}, {WY_SEC_SLOT_DEFAULTS, map({Pair{uval(3), integer(7)}})}, {WY_SEC_CODE, code_item({})}}), WY_ERR_IMAGE);
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(3)}, {WY_SEC_SLOT_DEFAULTS, map({Pair{uval(1), array({})}})}, {WY_SEC_CODE, code_item({})}}), WY_ERR_IMAGE);
    }

    TEST_CASE("statics and symbols refuse values of the wrong type")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        auto with = [&](std::uint8_t id, const Bytes& value) {
            return load_sections(ctx, {{WY_SEC_HEADER, header_with(0)}, {id, value}, {WY_SEC_CODE, code_item({})}});
        };
        CHECK_EQ(with(WY_SEC_STATICS, array({array({})})), WY_ERR_IMAGE);
        CHECK_EQ(with(WY_SEC_STATICS, array({map({})})), WY_ERR_IMAGE);
        CHECK_EQ(with(WY_SEC_STATICS, array({symbol("x")})), WY_ERR_IMAGE);
        CHECK_EQ(with(WY_SEC_STATICS, array({character(65)})), WY_ERR_IMAGE);
        CHECK_EQ(with(WY_SEC_SYMBOLS, array({text("ok"), uval(1)})), WY_ERR_IMAGE);
        CHECK_EQ(with(WY_SEC_SYMBOLS, array({byte_string({'a'})})), WY_ERR_IMAGE);
        CHECK_EQ(with(WY_SEC_SYMBOLS, array({text("a"), text("b")})), WY_ERR_NONE);
    }

    TEST_CASE("header fields are checked")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        auto with = [&](const Bytes& header) {
            return load_sections(ctx, {{WY_SEC_HEADER, header}, {WY_SEC_CODE, code_item({})}});
        };
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", uval(1)), kv("l", uval(2))})), WY_ERR_NONE);
        // `u` and any other extra key are ignored; the version is not a key.
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", uval(1)), kv("l", uval(2)), kv("u", array({uval(0)}))})), WY_ERR_NONE);
        CHECK_EQ(with(map({kv("g", uval(1)), kv("l", uval(2))})), WY_ERR_IMAGE);                       // no name
        CHECK_EQ(with(map({kv("n", text("m")), kv("l", uval(2))})), WY_ERR_IMAGE);                     // no g
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", uval(1))})), WY_ERR_IMAGE);                     // no l
        CHECK_EQ(with(map({kv("n", uval(5)), kv("g", uval(1)), kv("l", uval(2))})), WY_ERR_IMAGE);     // name not text
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", integer(-1)), kv("l", uval(2))})), WY_ERR_IMAGE);
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", float64(1.0)), kv("l", uval(2))})), WY_ERR_IMAGE);
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", uval(1)), kv("l", uval(0x10000))})), WY_ERR_IMAGE);
        CHECK_EQ(with(map({kv("n", text("m")), kv("g", uval(0x80000000ull)), kv("l", uval(2))})), WY_ERR_IMAGE);
        CHECK_EQ(with(array({})), WY_ERR_IMAGE);
    }

    TEST_CASE("function entries are range checked")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        auto fn = [](std::vector<Pair> pairs) { return map(pairs); };
        auto load = [&](const Bytes& entry) {
            return load_sections(ctx, {
                {WY_SEC_HEADER, header_with(2)},
                {WY_SEC_STATICS, array({text("s")})},
                {WY_SEC_FUNCTIONS, array({entry})},
                {WY_SEC_CODE, code_item({1, 2, 3})},
            });
        };
        std::vector<Pair> base = {kv("n", text("f")), kv("l", uval(2)), kv("c", uval(1)), kv("f", uval(0)), kv("p", array({}))};
        auto with = [&](const std::string& key, const Bytes& value) {
            std::vector<Pair> pairs;
            for (const Pair& p : base) { if (p.first != text(key)) { pairs.push_back(p); } }
            if (!value.empty()) { pairs.push_back(kv(key, value)); }
            return load(fn(pairs));
        };
        CHECK_EQ(load(fn(base)), WY_ERR_NONE);
        CHECK_EQ(with("c", uval(2)), WY_ERR_NONE);
        CHECK_EQ(with("c", uval(3)), WY_ERR_IMAGE);          // past the three code words
        CHECK_EQ(with("l", uval(0x10000)), WY_ERR_IMAGE);
        CHECK_EQ(with("f", uval(256)), WY_ERR_IMAGE);
        CHECK_EQ(with("n", Bytes{}), WY_ERR_IMAGE);          // required keys
        CHECK_EQ(with("l", Bytes{}), WY_ERR_IMAGE);
        CHECK_EQ(with("c", Bytes{}), WY_ERR_IMAGE);
        CHECK_EQ(with("f", Bytes{}), WY_ERR_IMAGE);
        CHECK_EQ(with("p", Bytes{}), WY_ERR_IMAGE);
        CHECK_EQ(with("p", array({map({kv("n", text("a")), kv("d", uval(0))})})), WY_ERR_NONE);
        CHECK_EQ(with("p", array({map({kv("n", text("a")), kv("d", uval(1))})})), WY_ERR_IMAGE);   // no such static
        CHECK_EQ(with("p", array({map({kv("d", uval(0))})})), WY_ERR_IMAGE);                      // no name
        CHECK_EQ(with("k", uval(3)), WY_ERR_NONE);
        CHECK_EQ(with("k", uval(0x10000)), WY_ERR_IMAGE);
        CHECK_EQ(with("r", uval(255)), WY_ERR_NONE);
        CHECK_EQ(with("r", uval(256)), WY_ERR_IMAGE);
        CHECK_EQ(with("t", array({uval(1)})), WY_ERR_NONE);
        CHECK_EQ(with("t", array({uval(2)})), WY_ERR_IMAGE); // past the two globals
        CHECK_EQ(with("t", uval(1)), WY_ERR_IMAGE);          // not an array
    }

    TEST_CASE("classes and messages decode and are range checked")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        auto fn = map({kv("n", text("f")), kv("l", uval(1)), kv("c", uval(0)), kv("f", uval(0)), kv("p", array({}))});
        auto cls = [](std::vector<Pair> extra) {
            std::vector<Pair> pairs = {kv("n", text("C")), kv("sl", array({map({kv("n", text("x")), kv("d", uval(0)), kv("g", uval(0))})})),
                kv("m", array({map({kv("y", uval(1)), kv("f", uval(0))})})), kv("st", array({map({kv("n", text("S")), kv("g", uval(1))})}))};
            for (const Pair& p : extra) { pairs.push_back(p); }
            return map(pairs);
        };
        auto load = [&](const Bytes& klass, wy_module** out = nullptr) {
            wy_module* local = nullptr;
            return load_sections(ctx, {
                {WY_SEC_HEADER, header_with(2)},
                {WY_SEC_STATICS, array({integer(5)})},
                {WY_SEC_SYMBOLS, array({text("a"), text("b")})},
                {WY_SEC_FUNCTIONS, array({fn})},
                {WY_SEC_CLASSES, array({klass})},
                {WY_SEC_MESSAGES, array({map({kv("p", array({uval(0), uval(1)}))})})},
                {WY_SEC_CODE, code_item({1})},
            }, out != nullptr ? out : &local);
        };
        wy_module* module = nullptr;
        REQUIRE_EQ(load(cls({kv("s", uval(1)), kv("i", uval(0))}), &module), WY_ERR_NONE);
        REQUIRE_EQ(module->class_count, 1u);
        const wy_class_proto& proto = module->class_protos[0];
        CHECK_EQ(proto.super_slot, 1);
        CHECK_EQ(proto.init_fn, 0);
        REQUIRE_EQ(proto.nslots, 1u);
        CHECK_EQ(proto.slots[0].default_static, 0);
        CHECK_EQ(proto.slots[0].getter_fn, 0);
        CHECK_EQ(proto.slots[0].setter_fn, -1);
        REQUIRE_EQ(proto.nmsgs, 1u);
        CHECK_EQ(proto.msgs[0].fn, 0u);
        REQUIRE_EQ(proto.nstatics, 1u);
        CHECK_EQ(proto.statics[0].global, 1u);
        REQUIRE_EQ(module->message_count, 1u);
        CHECK_EQ(module->messages[0].path_len, 2u);
        CHECK_EQ(module->messages[0].path[1], 1u);

        CHECK_EQ(load(cls({})), WY_ERR_NONE);
        CHECK_EQ(load(cls({kv("s", uval(2))})), WY_ERR_IMAGE);    // past the two globals
        CHECK_EQ(load(cls({kv("i", uval(1))})), WY_ERR_IMAGE);    // past the one function
        CHECK_EQ(load(map({kv("n", text("C"))})), WY_ERR_IMAGE);  // sl, m and st are required
        CHECK_EQ(load(map({kv("n", text("C")), kv("sl", array({})), kv("m", array({})), kv("st", array({}))})), WY_ERR_NONE);
        CHECK_EQ(load(map({kv("n", text("C")), kv("sl", array({})), kv("m", array({map({kv("y", uval(2)), kv("f", uval(0))})})), kv("st", array({}))})), WY_ERR_IMAGE);
        CHECK_EQ(load(map({kv("n", text("C")), kv("sl", array({})), kv("m", array({map({kv("y", uval(0)), kv("f", uval(1))})})), kv("st", array({}))})), WY_ERR_IMAGE);
        CHECK_EQ(load(map({kv("n", text("C")), kv("sl", array({map({kv("n", text("x")), kv("d", uval(1))})})), kv("m", array({})), kv("st", array({}))})), WY_ERR_IMAGE);
        CHECK_EQ(load(map({kv("n", text("C")), kv("sl", array({})), kv("m", array({})), kv("st", array({map({kv("n", text("S")), kv("g", uval(2))})}))})), WY_ERR_IMAGE);
        // A message path indexes the symbols.
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(0)}, {WY_SEC_SYMBOLS, array({text("a")})},
            {WY_SEC_MESSAGES, array({map({kv("p", array({uval(1)}))})})}, {WY_SEC_CODE, code_item({})}}), WY_ERR_IMAGE);
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(0)}, {WY_SEC_SYMBOLS, array({text("a")})},
            {WY_SEC_MESSAGES, array({map({kv("p", array({}))})})}, {WY_SEC_CODE, code_item({})}}), WY_ERR_IMAGE);
    }

    TEST_CASE("exports and free name global slots")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* module = nullptr;
        REQUIRE_EQ(load_sections(ctx, {
            {WY_SEC_HEADER, header_with(2)},
            {WY_SEC_CODE, code_item({})},
            {WY_SEC_EXPORTS, map({kv("a", uval(0))})},
            {WY_SEC_FREE, map({kv("b", uval(1)), kv("c", uval(0))})},
        }, &module), WY_ERR_NONE);
        CHECK_EQ(wy_slot_dict_get(&module->exports, intern(ctx, "a")), 0u);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx, "b")), 1u);
        CHECK_EQ(wy_slot_dict_get(&module->free_names, intern(ctx, "c")), 0u);

        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(2)}, {WY_SEC_CODE, code_item({})}, {WY_SEC_EXPORTS, map({kv("a", uval(2))})}}), WY_ERR_IMAGE);
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(2)}, {WY_SEC_CODE, code_item({})}, {WY_SEC_EXPORTS, map({kv("a", text("0"))})}}), WY_ERR_IMAGE);
        // An integer key is not a name.
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(2)}, {WY_SEC_CODE, code_item({})}, {WY_SEC_EXPORTS, map({Pair{uval(1), uval(0)}})}}), WY_ERR_IMAGE);
        // A name twice is a duplicate.
        CHECK_EQ(load_sections(ctx, {{WY_SEC_HEADER, header_with(2)}, {WY_SEC_CODE, code_item({})}, {WY_SEC_EXPORTS, map({kv("a", uval(0)), kv("a", uval(1))})}}), WY_ERR_IMAGE);
    }

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
