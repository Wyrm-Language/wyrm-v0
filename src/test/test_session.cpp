#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <wyrm.h>
#include <wyrm/bson.h>
#include <wyrm/gc.h>
#include <wyrm/image_loader.h>
#include <wyrm/opcode.h>
#include <wyrm/link.h>
#include <wyrm/module.h>
#include <wyrm/session.h>
#include <wyrm/string.h>

#include <test_common/test_context_fixture.h>
#include <test_common/test_fiber_fixture.h>

// REPL session module, milestone M0 (doc-llm/history/repl/repl-plan.md): the reservation
// mechanics. Appending real deltas is M1; here the tables are written by hand
// to prove that the reserved arrays do not move and that overruns are reported
// before anything changes.

namespace {

// A tiny BSON + container writer: just enough to hand-build delta images.
using Bytes = std::vector<std::uint8_t>;

void put_u32(Bytes& b, std::uint32_t v)
{
    for (int i = 0; i < 4; i++) { b.push_back((std::uint8_t) (v >> (8 * i))); }
}

Bytes element(std::uint8_t tag, const std::string& key, const Bytes& payload)
{
    Bytes e;
    e.push_back(tag);
    e.insert(e.end(), key.begin(), key.end());
    e.push_back(0);
    e.insert(e.end(), payload.begin(), payload.end());
    return e;
}

Bytes bson_i32(const std::string& key, std::int32_t v)
{
    Bytes p;
    put_u32(p, (std::uint32_t) v);
    return element(WY_BSON_TAG_I32, key, p);
}

Bytes bson_str(const std::string& key, const std::string& text)
{
    Bytes p;
    put_u32(p, (std::uint32_t) text.size() + 1);
    p.insert(p.end(), text.begin(), text.end());
    p.push_back(0);
    return element(WY_BSON_TAG_STRING, key, p);
}

Bytes document(const std::vector<Bytes>& elements)
{
    Bytes body;
    for (const Bytes& e : elements) { body.insert(body.end(), e.begin(), e.end()); }
    Bytes d;
    put_u32(d, (std::uint32_t) (body.size() + 5));
    d.insert(d.end(), body.begin(), body.end());
    d.push_back(0);
    return d;
}

// An array is a document whose keys are "0", "1", ...
Bytes array_of(const std::vector<Bytes>& items, std::uint8_t item_tag)
{
    std::vector<Bytes> elements;
    for (std::size_t i = 0; i < items.size(); i++) { elements.push_back(element(item_tag, std::to_string(i), items[i])); }
    return document(elements);
}

Bytes bson_doc(const std::string& key, const Bytes& d) { return element(WY_BSON_TAG_DOCUMENT, key, d); }

struct Delta
{
    std::int32_t bc = 0, bf = 0, bs = 0, by = 0, bk = 0, bm = 0, bg = 0;
    std::int32_t g = 0;   // globals after
    std::int32_t init = 0;
    bool delta_marker = true;
    std::vector<wy_u32> code;
    // functions: name, nlocals, absolute code offset
    struct Fn { std::string name; int nlocals; int code_offset; };
    std::vector<Fn> fns;
    std::vector<std::pair<std::string, int>> exports;
    std::vector<std::string> statics;   // string statics
    std::vector<std::string> symbols;
};

// Serialise `d` to a container. The returned buffer owns every section.
Bytes build_delta(const Delta& d)
{
    std::vector<Bytes> header = {bson_str("n", "__repl__"), bson_i32("v", 1)};
    if (d.delta_marker) { header.push_back(bson_i32("d", 1)); }
    header.push_back(bson_i32("g", d.g));
    header.push_back(bson_i32("l", 1));
    header.push_back(bson_i32("i", d.init));
    header.push_back(bson_i32("bc", d.bc)); header.push_back(bson_i32("bf", d.bf));
    header.push_back(bson_i32("bs", d.bs)); header.push_back(bson_i32("by", d.by));
    header.push_back(bson_i32("bk", d.bk)); header.push_back(bson_i32("bm", d.bm));
    header.push_back(bson_i32("bg", d.bg));

    std::vector<Bytes> fn_docs;
    for (const auto& f : d.fns) {
        fn_docs.push_back(document({bson_str("n", f.name), bson_i32("l", f.nlocals), bson_i32("c", f.code_offset),
            bson_i32("f", 0), element(WY_BSON_TAG_ARRAY, "p", document(std::vector<Bytes>{}))}));
    }
    std::vector<Bytes> exp;
    for (const auto& e : d.exports) { exp.push_back(bson_i32(e.first, e.second)); }

    struct Sec { std::uint8_t id; Bytes data; };
    std::vector<Sec> secs;
    secs.push_back({WY_SEC_HEADER, document(header)});
    if (!d.statics.empty()) {
        std::vector<Bytes> items;
        for (const auto& t : d.statics) {
            Bytes p;
            put_u32(p, (std::uint32_t) t.size() + 1);
            p.insert(p.end(), t.begin(), t.end());
            p.push_back(0);
            items.push_back(p);
        }
        secs.push_back({WY_SEC_STATICS, array_of(items, WY_BSON_TAG_STRING)});
    }
    if (!d.symbols.empty()) {
        std::vector<Bytes> items;
        for (const auto& t : d.symbols) {
            Bytes p;
            put_u32(p, (std::uint32_t) t.size() + 1);
            p.insert(p.end(), t.begin(), t.end());
            p.push_back(0);
            items.push_back(p);
        }
        secs.push_back({WY_SEC_SYMBOLS, array_of(items, WY_BSON_TAG_STRING)});
    }
    if (!d.fns.empty()) { secs.push_back({WY_SEC_FUNCTIONS, array_of(fn_docs, WY_BSON_TAG_DOCUMENT)}); }
    Bytes code;
    for (wy_u32 w : d.code) { put_u32(code, w); }
    secs.push_back({WY_SEC_CODE, code});
    if (!d.exports.empty()) { secs.push_back({WY_SEC_EXPORTS, document(exp)}); }

    Bytes out = {'W', 'Y', 'C', 0, 1, (std::uint8_t) secs.size(), 0, 0};
    std::uint32_t offset = 8 + 12 * (std::uint32_t) secs.size();
    std::vector<std::uint32_t> offsets;
    for (const Sec& sec : secs) {
        while (offset % 4 != 0) { offset++; }
        offsets.push_back(offset);
        offset += (std::uint32_t) sec.data.size();
    }
    for (std::size_t i = 0; i < secs.size(); i++) {
        out.push_back(secs[i].id); out.push_back(0); out.push_back(0); out.push_back(0);
        put_u32(out, offsets[i]);
        put_u32(out, (std::uint32_t) secs[i].data.size());
    }
    for (std::size_t i = 0; i < secs.size(); i++) {
        while (out.size() < offsets[i]) { out.push_back(0); }
        out.insert(out.end(), secs[i].data.begin(), secs[i].data.end());
    }
    return out;
}

wy_error extend_with(wy_context* ctx, wy_module* session, const Bytes& bytes, wy_uword* init = nullptr)
{
    wy_module_image image;
    wy_error err = wy_image_from_bytes(bytes.data(), bytes.size(), &image);
    if (err != WY_ERR_NONE) { return err; }
    return wy_module_extend(ctx, session, &image, init);
}

constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0) { return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op); }

// `L0 <- value; return L0`
std::vector<wy_u32> returns_const(int value)
{
    return {enc1(WY_OP_I8, (wy_u8) value, 0), enc1(WY_OP_RETURN, 1, 0)};
}

wy_word run_int(wy_context* ctx, wy_module* session, wy_uword fn)
{
    wy_value result = wy_value_nil();
    REQUIRE_EQ(wy_module_run_function(ctx, session, fn, &result), WY_ERR_NONE);
    REQUIRE_EQ(result.type, WY_TYPE_TAG_WORD);
    return result.data.word;
}

wy_uword export_slot(wy_context* ctx, wy_module* session, const char* name)
{
    wy_symbol sym = WY_NULL;
    REQUIRE_EQ(wy_context_intern(ctx, name, std::strlen(name), &sym), WY_ERR_NONE);
    return wy_slot_dict_get(&session->exports, sym);
}

wy_session_config small_config(wy_uword n)
{
    wy_session_config config;
    for (int t = 0; t < WY_SESSION_TABLE_COUNT; t++) { config.capacity[t] = n; }
    return config;
}

}  // namespace

TEST_SUITE("session")
{
    TEST_CASE("defaults reserve 16 MiB of code and start empty")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_session_config config;
        wy_session_config_default(&config);
        CHECK_EQ(config.capacity[WY_SESSION_CODE], 4u * 1024u * 1024u);
        CHECK_EQ(config.capacity[WY_SESSION_CODE] * sizeof(wy_u32), 16u * 1024u * 1024u);

        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);
        REQUIRE_NE(session, WY_NULL);
        CHECK(wy_module_is_session(session));
        CHECK_EQ(session->state, WY_MODULE_READY);
        for (int t = 0; t < WY_SESSION_TABLE_COUNT; t++) {
            CAPTURE(t);
            CHECK_EQ(wy_session_capacity(session, (wy_session_table) t), config.capacity[t]);
            CHECK_EQ(wy_session_used(session, (wy_session_table) t), 0u);
        }
        CHECK_NE(session->code, WY_NULL);
        CHECK_NE(session->functions, WY_NULL);
    }

    TEST_CASE("an ordinary module is not a session")
    {
        test_context_fixture fix;
        wy_module* plain = wy_module_new_f(fix.get_context_ptr());
        REQUIRE_NE(plain, WY_NULL);
        CHECK_FALSE(wy_module_is_session(plain));
        CHECK_EQ(wy_session_capacity(plain, WY_SESSION_CODE), 0u);
        CHECK_EQ(wy_session_check_room(plain, WY_SESSION_CODE, 1), WY_ERR_INVAL);
    }

    TEST_CASE("invalid configurations are refused")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_module* out = WY_NULL;

        CHECK_EQ(wy_module_session_new(ctx, WY_NULL, &out), WY_ERR_INVAL);
        wy_session_config config = small_config(8);
        CHECK_EQ(wy_module_session_new(ctx, &config, WY_NULL), WY_ERR_INVAL);
        config.capacity[WY_SESSION_STATICS] = 0;
        CHECK_EQ(wy_module_session_new(ctx, &config, &out), WY_ERR_INVAL);
        CHECK_EQ(out, WY_NULL);
    }

    TEST_CASE("a reservation that cannot be allocated fails cleanly")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(8);
        // count * sizeof(element) overflows: refused as NOMEM, not a wild allocation.
        config.capacity[WY_SESSION_FUNCTIONS] = ((wy_uword) -1) / 2;
        wy_module* out = WY_NULL;
        CHECK_EQ(wy_module_session_new(ctx, &config, &out), WY_ERR_NOMEM);
        CHECK_EQ(out, WY_NULL);
    }

    TEST_CASE("check_room reports an overrun before anything changes")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(4);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        CHECK_EQ(wy_session_check_room(session, WY_SESSION_CODE, 4), WY_ERR_NONE);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_CODE, 5), WY_ERR_SESSION_FULL);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_CODE, ((wy_uword) -1)), WY_ERR_SESSION_FULL);

        session->code_len = 3;  // stand-in for an M1 append
        CHECK_EQ(wy_session_used(session, WY_SESSION_CODE), 3u);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_CODE, 1), WY_ERR_NONE);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_CODE, 2), WY_ERR_SESSION_FULL);
        CHECK_EQ(session->code_len, 3u);  // the check itself changed nothing
    }

    TEST_CASE("reserved arrays never move as the tables fill")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        const wy_u32* code = session->code;
        wy_function_proto* functions = session->functions;
        wy_value* statics = session->statics;
        wy_value* globals = session->globals;
        wy_symbol* symbols = session->symbols;

        // Fill each table to its capacity by hand, the way an extend will.
        wy_u32* code_w = const_cast<wy_u32*>(session->code);
        for (wy_uword i = 0; i < 64; i++) {
            code_w[i] = (wy_u32) i;
            session->functions[i] = wy_function_proto{};
            session->statics[i] = wy_value_word((wy_word) i);
            session->globals[i] = wy_value_word((wy_word) (i * 2));
        }
        session->code_len = session->function_count = session->static_count = session->global_count = 64;

        CHECK_EQ(session->code, code);
        CHECK_EQ(session->functions, functions);
        CHECK_EQ(session->statics, statics);
        CHECK_EQ(session->globals, globals);
        CHECK_EQ(session->symbols, symbols);
        CHECK_EQ(session->code[63], 63u);
        CHECK_EQ(session->statics[10].data.word, 10);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_CODE, 1), WY_ERR_SESSION_FULL);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_FUNCTIONS, 1), WY_ERR_SESSION_FULL);
        CHECK_EQ(wy_session_check_room(session, WY_SESSION_SYMBOLS, 1), WY_ERR_NONE);
    }

    TEST_CASE("the session is registered under __repl__ and survives a collection")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(8);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);
        session->globals[0] = wy_value_word(41);
        session->global_count = 1;

        wy_string* path = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(ctx, "__repl__", &path), WY_ERR_NONE);
        wy_module* found = WY_NULL;
        REQUIRE_EQ(wy_link_import(ctx, path, &found), WY_ERR_NONE);
        CHECK_EQ(found, session);

        wy_context_gc_full_run(ctx);  // the real collection: registered modules are roots
        CHECK_EQ(session->globals[0].data.word, 41);
        CHECK(wy_module_is_session(session));
    }

    TEST_CASE("extend appends a delta, runs it, and a second delta extends the first in place")
    {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        Delta d1;
        d1.g = 1; d1.init = 0;
        d1.code = returns_const(5);
        d1.fns = {{"main1", 1, 0}};
        d1.exports = {{"x", 0}};
        wy_uword init = 99;
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d1), &init), WY_ERR_NONE);
        CHECK_EQ(init, 0u);
        CHECK_EQ(session->code_len, 2u);
        CHECK_EQ(session->function_count, 1u);
        CHECK_EQ(session->global_count, 1u);
        CHECK_EQ(export_slot(ctx, session, "x"), 0u);
        CHECK_EQ(run_int(ctx, session, 0), 5);

        const wy_u32* code = session->code;
        wy_function_proto* functions = session->functions;
        const wy_u32 first_word = session->code[0];

        Delta d2;
        d2.bc = 2; d2.bf = 1; d2.bg = 1; d2.g = 2; d2.init = 1;
        d2.code = returns_const(9);
        d2.fns = {{"main2", 1, 2}};          // absolute offset: after the first delta's code
        d2.exports = {{"x", 1}};             // shadows x with a new slot
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d2), &init), WY_ERR_NONE);
        CHECK_EQ(init, 1u);
        CHECK_EQ(session->code_len, 4u);
        CHECK_EQ(session->function_count, 2u);
        CHECK_EQ(session->global_count, 2u);

        // Nothing moved, and the first delta's code and function are untouched.
        CHECK_EQ(session->code, code);
        CHECK_EQ(session->functions, functions);
        CHECK_EQ(session->code[0], first_word);
        CHECK_EQ(run_int(ctx, session, 0), 5);
        CHECK_EQ(run_int(ctx, session, 1), 9);
        CHECK_EQ(export_slot(ctx, session, "x"), 1u);  // the name now means the newest binding
        CHECK_EQ(session->state, WY_MODULE_READY);
    }

    TEST_CASE("a delta compiled against different counts is refused and changes nothing")
    {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        Delta d;
        d.g = 1; d.code = returns_const(1); d.fns = {{"m", 1, 0}};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d)), WY_ERR_NONE);

        Delta stale = d;        // still thinks the session is empty
        stale.bc = 0;
        CHECK_EQ(extend_with(ctx, session, build_delta(stale)), WY_ERR_IMAGE);
        Delta ahead = d;        // believes there are more functions than there are
        ahead.bc = 2; ahead.bf = 5; ahead.bg = 1; ahead.g = 2; ahead.init = 5;
        CHECK_EQ(extend_with(ctx, session, build_delta(ahead)), WY_ERR_IMAGE);
        Delta plain = d;        // not marked as a delta at all
        plain.bc = 2; plain.bf = 1; plain.bg = 1; plain.g = 2; plain.init = 1; plain.delta_marker = false;
        CHECK_EQ(extend_with(ctx, session, build_delta(plain)), WY_ERR_IMAGE);

        CHECK_EQ(session->code_len, 2u);
        CHECK_EQ(session->function_count, 1u);
        CHECK_EQ(session->global_count, 1u);
        CHECK_EQ(run_int(ctx, session, 0), 1);
    }

    TEST_CASE("a malformed delta rolls back completely and the next good one still applies")
    {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        Delta bad;
        bad.g = 1; bad.code = returns_const(1);
        bad.fns = {{"ok", 1, 0}, {"broken", 1, 99}};   // second function's code offset is out of range
        bad.init = 0;
        CHECK_EQ(extend_with(ctx, session, build_delta(bad)), WY_ERR_IMAGE);
        CHECK_EQ(session->code_len, 0u);
        CHECK_EQ(session->function_count, 0u);
        CHECK_EQ(session->global_count, 0u);

        Delta noinit = bad;      // init function index outside this delta
        noinit.fns = {{"ok", 1, 0}};
        noinit.init = 3;
        CHECK_EQ(extend_with(ctx, session, build_delta(noinit)), WY_ERR_IMAGE);
        CHECK_EQ(session->function_count, 0u);

        Delta good;
        good.g = 1; good.code = returns_const(7); good.fns = {{"m", 1, 0}};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(good)), WY_ERR_NONE);
        CHECK_EQ(run_int(ctx, session, 0), 7);
    }

    TEST_CASE("an extend that would overrun a reservation is refused before anything changes")
    {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        config.capacity[WY_SESSION_CODE] = 3;           // room for one 2-word delta only
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        Delta d1;
        d1.g = 1; d1.code = returns_const(1); d1.fns = {{"m", 1, 0}};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d1)), WY_ERR_NONE);

        Delta d2;
        d2.bc = 2; d2.bf = 1; d2.bg = 1; d2.g = 2; d2.init = 1;
        d2.code = returns_const(2); d2.fns = {{"m2", 1, 2}};
        CHECK_EQ(extend_with(ctx, session, build_delta(d2)), WY_ERR_SESSION_FULL);
        CHECK_EQ(session->code_len, 2u);
        CHECK_EQ(session->function_count, 1u);
        CHECK_EQ(run_int(ctx, session, 0), 1);
    }

    TEST_CASE("a faulting input does not poison the session")
    {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        Delta d1;
        d1.g = 1; d1.code = returns_const(4); d1.fns = {{"ok", 1, 0}};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d1)), WY_ERR_NONE);

        Delta d2;
        d2.bc = 2; d2.bf = 1; d2.bg = 1; d2.g = 1; d2.init = 1;
        d2.code = {enc1(WY_OP_TRAP, 5, 0)};              // `trap(5)`
        d2.fns = {{"boom", 1, 2}};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d2)), WY_ERR_NONE);

        wy_value result = wy_value_nil();
        CHECK_NE(wy_module_run_function(ctx, session, 1, &result), WY_ERR_NONE);
        CHECK_EQ(session->state, WY_MODULE_READY);
        CHECK_EQ(wy_module_run_function(ctx, session, 7, &result), WY_ERR_RANGE);
        CHECK_EQ(run_int(ctx, session, 0), 4);           // still fully usable
    }

    TEST_CASE("statics and symbols are numbered absolutely across deltas")
    {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(64);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        // `L0 <- static[index]; return L0`
        auto returns_static = [](int index) {
            return std::vector<wy_u32>{enc1(WY_OP_LCONST, 0, (wy_u16) index), enc1(WY_OP_RETURN, 1, 0)};
        };

        Delta d1;
        d1.g = 1; d1.code = returns_static(0); d1.fns = {{"a", 1, 0}};
        d1.statics = {"first"}; d1.symbols = {"alpha", "beta"};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d1)), WY_ERR_NONE);
        CHECK_EQ(session->static_count, 1u);
        CHECK_EQ(session->symbol_count, 2u);

        Delta d2;
        d2.bc = 2; d2.bf = 1; d2.bs = 1; d2.by = 2; d2.bg = 1; d2.g = 1; d2.init = 1;
        d2.code = returns_static(1);                    // static 1 is this delta's first
        d2.fns = {{"b", 1, 2}};
        d2.statics = {"second"}; d2.symbols = {"gamma"};
        wy_value* statics_before = session->statics;
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d2)), WY_ERR_NONE);
        CHECK_EQ(session->statics, statics_before);
        CHECK_EQ(session->static_count, 2u);
        CHECK_EQ(session->symbol_count, 3u);

        for (wy_uword i = 0; i < 2; i++) {
            wy_value v = wy_value_nil();
            REQUIRE_EQ(wy_module_run_function(ctx, session, i, &v), WY_ERR_NONE);
            REQUIRE_EQ(v.type, WY_TYPE_TAG_STR);
            const char* want = i == 0 ? "first" : "second";
            CHECK_EQ(std::string(v.data.str->str, v.data.str->len), std::string(want));
        }

        // A third delta still numbers from the running totals (this one reuses static 0).
        Delta d3;
        d3.bc = 4; d3.bf = 2; d3.bs = 2; d3.by = 3; d3.bg = 1; d3.g = 1; d3.init = 2;
        d3.code = returns_static(0); d3.fns = {{"c", 1, 4}};
        d3.statics = {"third"};
        REQUIRE_EQ(extend_with(ctx, session, build_delta(d3)), WY_ERR_NONE);
        CHECK_EQ(session->function_count, 3u);
    }

    TEST_CASE("unregistering a session lets the next collection free it")
    {
        test_context_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_session_config config = small_config(8);
        wy_module* session = WY_NULL;
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &session), WY_ERR_NONE);

        auto in_arena = [&](const wy_object* object) {
            for (wy_object* cur = ctx->arena.first; cur; cur = cur->next) {
                if (cur == object) { return true; }
            }
            return false;
        };
        const wy_object* object = WY_MODULE_GET_OBJ(session);
        wy_context_gc_full_run(ctx);
        CHECK(in_arena(object));                        // registered: a root, so it stays

        REQUIRE_EQ(wy_context_module_unregister(ctx, session), WY_ERR_NONE);
        CHECK_EQ(wy_context_module_unregister(ctx, session), WY_ERR_UNBOUND);  // not registered any more
        wy_string* path = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(ctx, "__repl__", &path), WY_ERR_NONE);
        wy_module* found = WY_NULL;
        CHECK_NE(wy_link_import(ctx, path, &found), WY_ERR_NONE);   // and no longer importable

        wy_context_gc_full_run(ctx);
        CHECK_FALSE(in_arena(object));                  // unreferenced: collected, reservation freed
    }
}
