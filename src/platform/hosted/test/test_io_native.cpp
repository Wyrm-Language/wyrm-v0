#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/bytes.h>
#include <wyrm/coroutine.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/link.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/opcode.h>
#include <wyrm/platform/hosted/io_native.h>
#include <wyrm/slot.h>
#include <wyrm/string.h>
#include <wyrm/vm.h>

#include <test_common/test_fiber_fixture.h>

/**
 * `std::io` natives (epic 5 M4): open/read/write/lseek/dup2/close/flush as
 * exec natives over real POSIX fds (pypoc/wypoc/wyrm_io.py's reference
 * semantics). Every call is hand-packed bytecode (no compiler in this repo)
 * driven through wy_vm_call_sync, the same convention test_coroutine.cpp
 * uses for exec natives - wy_vm_call_sync itself only accepts a
 * WY_TYPE_TAG_FUNCTION callee, so an exec native can only be reached from a
 * bytecode CALL.
 */

namespace {

constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}
constexpr wy_u32 enc2a(wy_u8 op, wy_u8 f, wy_u16 a0) { return enc1(op, f, a0); }
constexpr wy_u32 enc2b(wy_u16 a1, wy_u16 a2) { return (wy_u32(a1) << 16) | wy_u32(a2); }

wy_function_proto proto_at(wy_u32 offset, wy_u16 nlocals, wy_u8 flags = 0)
{
    wy_function_proto proto = {};
    proto.code_offset = offset;
    proto.nlocals = nlocals;
    proto.flags = flags;
    return proto;
}

wy_module* make_code_module(wy_context* ctx, const std::vector<wy_u32>& code,
    const std::vector<wy_function_proto>& protos, const std::vector<wy_value>& statics)
{
    wy_module* module = wy_module_new_f(ctx);
    auto* heap_code = (wy_u32*) wy_context_gc_alloc(ctx, sizeof(wy_u32) * code.size());
    std::copy(code.begin(), code.end(), heap_code);
    module->code = heap_code;
    module->code_len = code.size();

    if (!statics.empty()) {
        auto* heap_statics = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * statics.size());
        std::copy(statics.begin(), statics.end(), heap_statics);
        module->statics = heap_statics;
        module->static_count = statics.size();
    }
    module->function_count = protos.size();
    auto* heap_protos = (wy_function_proto*) wy_context_gc_alloc(ctx, sizeof(wy_function_proto) * protos.size());
    std::copy(protos.begin(), protos.end(), heap_protos);
    module->functions = heap_protos;
    return module;
}

wy_string* make_string(wy_context* ctx, const char* text)
{
    wy_string* s = WY_NULL;
    REQUIRE_EQ(wy_string_strdup(ctx, text, &s), WY_ERR_NONE);
    return s;
}

/** Call `native(args...)`, capturing `nres` results, via one hand-packed
 * bytecode function: LCONST the native and every arg from statics, CALL,
 * RETURN. `native` and `args` become statics 0..argc; results land at
 * L0..L(nres-1). */
std::vector<wy_value> call_native(wy_context* ctx, wy_value native,
    const std::vector<wy_value>& args, wy_uword nres)
{
    std::vector<wy_value> statics;
    statics.push_back(native);
    statics.insert(statics.end(), args.begin(), args.end());

    std::vector<wy_u32> code;
    for (wy_uword i = 0; i <= args.size(); i++) {
        code.push_back(enc1(WY_OP_LCONST, (wy_u8) i, (wy_u16) i));  // L_i <- static_i
    }
    code.push_back(enc2a(WY_OP_CALL, (wy_u8) args.size(), 0));      // base=L0, argc=args.size()
    code.push_back(enc2b((wy_u16) nres, 0));
    code.push_back(enc1(WY_OP_RETURN, (wy_u8) nres, 0));            // return L0..L(nres-1)

    std::vector<wy_function_proto> protos = {proto_at(0, (wy_u16) (args.size() + 1 + nres), 0)};
    wy_module* module = make_code_module(ctx, code, protos, statics);
    REQUIRE_EQ(wy_context_module_register(ctx, module, nullptr), WY_ERR_NONE);

    wy_function* fn0 = WY_NULL;
    REQUIRE_EQ(wy_function_new(ctx, module, &module->functions[0], WY_NULL, 0, &fn0), WY_ERR_NONE);

    std::vector<wy_value> out(nres);
    wy_error err = wy_vm_call_sync(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn0),
        WY_NULL, 0, out.data(), nres);
    REQUIRE_EQ(err, WY_ERR_NONE);
    return out;
}

wy_value native_value(wy_context* ctx, wy_module* io, const char* name)
{
    wy_symbol sym = WY_NULL;
    REQUIRE_EQ(wy_context_intern(ctx, name, std::strlen(name), &sym), WY_ERR_NONE);
    wy_uword slot = wy_slot_dict_get(&io->exports, sym);
    REQUIRE_NE(slot, WY_SLOT_INVALID);
    return io->globals[slot];
}

wy_value str_value(wy_context* ctx, const char* text) { return wy_value_object(WY_TYPE_TAG_STR, (wy_object*) make_string(ctx, text)); }

bool is_os_error(wy_context* ctx, wy_value v)
{
    return v.type == WY_TYPE_TAG_ERROR && v.data.gc_object != WY_NULL &&
        ((wy_error_obj*) v.data.gc_object)->cls == ctx->os_error_class;
}

struct temp_file
{
    std::filesystem::path path;
    temp_file()
        : path(std::filesystem::temp_directory_path() /
              ("wyrm_io_native_test_" + std::to_string(static_cast<long>(::getpid())) + "_" +
               std::to_string(++counter)))
    {
    }
    ~temp_file()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    static int counter;
};
int temp_file::counter = 0;

} // namespace

TEST_SUITE("io_native")
{
    TEST_CASE("open/write/read-back/close round trip on a real file") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;
        wy_module* io = WY_NULL;
        REQUIRE_EQ(wy_io_module_new(ctx, &io), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_module_register(ctx, io, nullptr), WY_ERR_NONE);

        temp_file tf;
        wy_value open_fn = native_value(ctx, io, "open");
        wy_value write_fn = native_value(ctx, io, "write");
        wy_value read_fn = native_value(ctx, io, "read");
        wy_value lseek_fn = native_value(ctx, io, "lseek");
        wy_value close_fn = native_value(ctx, io, "close");

        auto opened = call_native(ctx, open_fn, {str_value(ctx, tf.path.c_str()), str_value(ctx, "w+")}, 1);
        REQUIRE_EQ(opened[0].type, WY_TYPE_TAG_WORD);
        wy_word handle = opened[0].data.word;

        auto written = call_native(ctx, write_fn, {wy_value_word(handle), str_value(ctx, "hello wyrm")}, 1);
        REQUIRE_EQ(written[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(written[0].data.word, 10);

        auto sought = call_native(ctx, lseek_fn, {wy_value_word(handle), wy_value_word(0), wy_value_word(0)}, 1);
        REQUIRE_EQ(sought[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(sought[0].data.word, 0);

        auto read_back = call_native(ctx, read_fn, {wy_value_word(handle), wy_value_word(-1)}, 1);
        REQUIRE_EQ(read_back[0].type, WY_TYPE_TAG_STR);
        CHECK_EQ(std::string(read_back[0].data.str->str, read_back[0].data.str->len), "hello wyrm");

        auto closed = call_native(ctx, close_fn, {wy_value_word(handle)}, 1);
        REQUIRE_EQ(closed[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(closed[0].data.word, 0);
    }

    TEST_CASE("epic 7 M4: write accepts bytes and read_bytes round-trips binary data") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;
        wy_module* io = WY_NULL;
        REQUIRE_EQ(wy_io_module_new(ctx, &io), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_module_register(ctx, io, nullptr), WY_ERR_NONE);

        temp_file tf;
        wy_value open_fn = native_value(ctx, io, "open");
        wy_value write_fn = native_value(ctx, io, "write");
        wy_value read_bytes_fn = native_value(ctx, io, "read_bytes");
        wy_value lseek_fn = native_value(ctx, io, "lseek");
        wy_value close_fn = native_value(ctx, io, "close");

        // Includes a NUL and a high (non-ASCII, non-UTF-8) byte - exactly
        // the case a str-only write/read pair can't carry losslessly.
        const wy_u8 payload[] = {0x00, 0xFF, 0x10, 0xAB, 0xCD};
        wy_bytes* payload_bytes = WY_NULL;
        REQUIRE_EQ(wy_bytes_new(ctx, payload, sizeof(payload), &payload_bytes), WY_ERR_NONE);
        wy_value payload_v = wy_value_object(WY_TYPE_TAG_BYTES, (wy_object*) payload_bytes);

        auto opened = call_native(ctx, open_fn, {str_value(ctx, tf.path.c_str()), str_value(ctx, "w+b")}, 1);
        REQUIRE_EQ(opened[0].type, WY_TYPE_TAG_WORD);
        wy_word handle = opened[0].data.word;

        auto written = call_native(ctx, write_fn, {wy_value_word(handle), payload_v}, 1);
        REQUIRE_EQ(written[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(written[0].data.word, (wy_word) sizeof(payload));

        auto sought = call_native(ctx, lseek_fn, {wy_value_word(handle), wy_value_word(0), wy_value_word(0)}, 1);
        REQUIRE_EQ(sought[0].type, WY_TYPE_TAG_WORD);

        auto read_back = call_native(ctx, read_bytes_fn, {wy_value_word(handle), wy_value_word(-1)}, 1);
        REQUIRE_EQ(read_back[0].type, WY_TYPE_TAG_BYTES);
        wy_bytes* result = (wy_bytes*) read_back[0].data.gc_object;
        REQUIRE_EQ(result->len, sizeof(payload));
        CHECK_EQ(std::memcmp(result->data, payload, sizeof(payload)), 0);

        auto closed = call_native(ctx, close_fn, {wy_value_word(handle)}, 1);
        REQUIRE_EQ(closed[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(closed[0].data.word, 0);
    }

    TEST_CASE("open on a missing file (read mode) answers an OSError value, not a fault") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;
        wy_module* io = WY_NULL;
        REQUIRE_EQ(wy_io_module_new(ctx, &io), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_module_register(ctx, io, nullptr), WY_ERR_NONE);

        wy_value open_fn = native_value(ctx, io, "open");
        auto opened = call_native(ctx, open_fn,
            {str_value(ctx, "/nonexistent/wyrm-io-native-test-path"), str_value(ctx, "r")}, 1);
        CHECK(is_os_error(ctx, opened[0]));
    }

    TEST_CASE("dup2 makes new refer to old, and close forgets the handle") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;
        wy_module* io = WY_NULL;
        REQUIRE_EQ(wy_io_module_new(ctx, &io), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_module_register(ctx, io, nullptr), WY_ERR_NONE);

        temp_file tf;
        wy_value open_fn = native_value(ctx, io, "open");
        wy_value dup2_fn = native_value(ctx, io, "dup2");
        wy_value write_fn = native_value(ctx, io, "write");
        wy_value read_fn = native_value(ctx, io, "read");
        wy_value close_fn = native_value(ctx, io, "close");
        wy_value lseek_fn = native_value(ctx, io, "lseek");

        auto opened = call_native(ctx, open_fn, {str_value(ctx, tf.path.c_str()), str_value(ctx, "w+")}, 1);
        wy_word handle = opened[0].data.word;
        wy_word dup_handle = handle + 1000;  // an fd unlikely to be in use

        auto duped = call_native(ctx, dup2_fn, {wy_value_word(handle), wy_value_word(dup_handle)}, 1);
        REQUIRE_EQ(duped[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(duped[0].data.word, dup_handle);

        call_native(ctx, write_fn, {wy_value_word(dup_handle), str_value(ctx, "via dup")}, 1);
        call_native(ctx, lseek_fn, {wy_value_word(handle), wy_value_word(0), wy_value_word(0)}, 1);
        auto read_back = call_native(ctx, read_fn, {wy_value_word(handle), wy_value_word(-1)}, 1);
        CHECK_EQ(std::string(read_back[0].data.str->str, read_back[0].data.str->len), "via dup");

        call_native(ctx, close_fn, {wy_value_word(handle)}, 1);
        call_native(ctx, close_fn, {wy_value_word(dup_handle)}, 1);
    }

    TEST_CASE("flush on a regular file's handle succeeds") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;
        wy_module* io = WY_NULL;
        REQUIRE_EQ(wy_io_module_new(ctx, &io), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_module_register(ctx, io, nullptr), WY_ERR_NONE);

        temp_file tf;
        wy_value open_fn = native_value(ctx, io, "open");
        wy_value flush_fn = native_value(ctx, io, "flush");
        wy_value close_fn = native_value(ctx, io, "close");

        auto opened = call_native(ctx, open_fn, {str_value(ctx, tf.path.c_str()), str_value(ctx, "w+")}, 1);
        wy_word handle = opened[0].data.word;

        auto flushed = call_native(ctx, flush_fn, {wy_value_word(handle)}, 1);
        REQUIRE_EQ(flushed[0].type, WY_TYPE_TAG_WORD);
        CHECK_EQ(flushed[0].data.word, 0);

        call_native(ctx, close_fn, {wy_value_word(handle)}, 1);
    }

    TEST_CASE("wy_io_module_install registers the module under import path std::io") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        REQUIRE_EQ(wy_io_module_install(ctx), WY_ERR_NONE);

        wy_string* path = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(ctx, "std::io", &path), WY_ERR_NONE);
        wy_module* found = WY_NULL;
        REQUIRE_EQ(wy_link_import(ctx, path, &found), WY_ERR_NONE);
        REQUIRE_NE(found, WY_NULL);
        CHECK_EQ(found->state, WY_MODULE_BUILTIN);
    }
}
