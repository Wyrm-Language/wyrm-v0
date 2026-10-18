#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <wyrm/cbor.h>

#include <test_common/cbor_build.h>

// The strict reader for the wire encoding (project/design/wire_encoding.md):
// every row of its Worked examples and Never emitted tables, plus the limits
// the reader enforces on top.

namespace
{

using wy_test_cbor::Bytes;

wy_error read_one(const Bytes& b, wy_cbor_item* item)
{
    wy_cbor_reader reader;
    wy_cbor_reader_init(&reader, b.data(), b.size());
    return wy_cbor_read(&reader, item);
}

wy_cbor_span span_of(const Bytes& b) { return wy_cbor_span{b.data(), b.size()}; }

// A whole buffer that is exactly one valid item.
bool is_valid(const Bytes& b) { return wy_cbor_validate(b.data(), b.size()) == WY_ERR_NONE; }

}  // namespace

TEST_SUITE("cbor")
{
    TEST_CASE("worked examples: integers")
    {
        wy_cbor_item it;
        REQUIRE_EQ(read_one({0x00}, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_UINT);
        CHECK_EQ(it.n, 0u);
        REQUIRE_EQ(read_one({0x17}, &it), WY_ERR_NONE);
        CHECK_EQ(it.n, 23u);
        REQUIRE_EQ(read_one({0x18, 0x18}, &it), WY_ERR_NONE);
        CHECK_EQ(it.n, 24u);
        REQUIRE_EQ(read_one({0x19, 0x01, 0xF4}, &it), WY_ERR_NONE);
        CHECK_EQ(it.n, 500u);

        wy_i64 v;
        Bytes minus_one = {0x20};
        REQUIRE_EQ(read_one(minus_one, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_NEGINT);
        CHECK_EQ(it.n, 0u);
        REQUIRE_EQ(wy_cbor_get_i64(span_of(minus_one), &v), WY_ERR_NONE);
        CHECK_EQ(v, -1);

        Bytes minus_500 = {0x39, 0x01, 0xF3};
        REQUIRE_EQ(wy_cbor_get_i64(span_of(minus_500), &v), WY_ERR_NONE);
        CHECK_EQ(v, -500);

        Bytes int64_min = {0x3B, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        REQUIRE_EQ(wy_cbor_get_i64(span_of(int64_min), &v), WY_ERR_NONE);
        CHECK_EQ(v, INT64_MIN);
        // One further would be -2^63 - 1: no int64.
        Bytes below_min = {0x3B, 0x80, 0, 0, 0, 0, 0, 0, 0};
        CHECK_EQ(wy_cbor_get_i64(span_of(below_min), &v), WY_ERR_IMAGE);
    }

    TEST_CASE("worked examples: floats, bool, nil")
    {
        wy_cbor_item it;
        REQUIRE_EQ(read_one({0xFA, 0x3F, 0xC0, 0x00, 0x00}, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_FLOAT32);
        CHECK_EQ(it.f, 1.5);
        REQUIRE_EQ(read_one({0xFB, 0x3F, 0xF8, 0, 0, 0, 0, 0, 0}, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_FLOAT64);
        CHECK_EQ(it.f, 1.5);
        REQUIRE_EQ(read_one({0xF5}, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_TRUE);
        REQUIRE_EQ(read_one({0xF4}, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_FALSE);
        REQUIRE_EQ(read_one({0xF6}, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_NIL);
    }

    TEST_CASE("NaN and infinities pass through in both widths")
    {
        wy_cbor_item it;
        REQUIRE_EQ(read_one({0xFB, 0x7F, 0xF8, 0, 0, 0, 0, 0, 0}, &it), WY_ERR_NONE);
        CHECK(std::isnan(it.f));
        REQUIRE_EQ(read_one({0xFA, 0x7F, 0xC0, 0, 0}, &it), WY_ERR_NONE);
        CHECK(std::isnan(it.f));
        REQUIRE_EQ(read_one({0xFA, 0xFF, 0x80, 0, 0}, &it), WY_ERR_NONE);
        CHECK(std::isinf(it.f));
        CHECK_LT(it.f, 0.0);
        REQUIRE_EQ(read_one({0xFB, 0x7F, 0xF0, 0, 0, 0, 0, 0, 0}, &it), WY_ERR_NONE);
        CHECK(std::isinf(it.f));
    }

    TEST_CASE("worked examples: strings, symbols, chars, bytes")
    {
        // The items point into these buffers, so they outlive the reads.
        Bytes hi = {0x62, 0x68, 0x69};
        Bytes sym = {0xD9, 0x01, 0x18, 0x61, 0x78};
        Bytes ch = {0xD9, 0x01, 0x1A, 0x18, 0x61};
        Bytes raw = {0x42, 0x01, 0x02};
        wy_cbor_item it;
        REQUIRE_EQ(read_one(hi, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_TEXT);
        CHECK_EQ(it.n, 2u);
        CHECK_EQ(std::string((const char*) it.data, it.n), "hi");

        REQUIRE_EQ(read_one(sym, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_SYMBOL);
        CHECK_EQ(std::string((const char*) it.data, it.n), "x");

        REQUIRE_EQ(read_one(ch, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_CHAR);
        CHECK_EQ(it.n, 0x61u);

        REQUIRE_EQ(read_one(raw, &it), WY_ERR_NONE);
        CHECK_EQ(it.kind, WY_CBOR_BYTES);
        CHECK_EQ(it.n, 2u);
        CHECK_EQ(it.data[1], 2);
    }

    TEST_CASE("worked examples: containers")
    {
        Bytes nested = {0x82, 0x01, 0x82, 0x02, 0x03};
        CHECK(is_valid(nested));
        wy_cbor_reader reader;
        wy_uword count;
        REQUIRE_EQ(wy_cbor_open_array(span_of(nested), &reader, &count), WY_ERR_NONE);
        CHECK_EQ(count, 2u);
        wy_cbor_span first, second;
        REQUIRE_EQ(wy_cbor_next_span(&reader, &first), WY_ERR_NONE);
        REQUIRE_EQ(wy_cbor_next_span(&reader, &second), WY_ERR_NONE);
        CHECK_EQ(first.len, 1u);
        CHECK_EQ(second.len, 3u);
        CHECK(wy_cbor_reader_at_end(&reader));
        CHECK(wy_cbor_span_is_array(second));
        CHECK_FALSE(wy_cbor_span_is_map(second));

        Bytes dict = {0xA2, 0x61, 0x61, 0x01, 0xD9, 0x01, 0x18, 0x61, 0x78, 0x21};
        CHECK_EQ(dict.size(), 10u);
        CHECK(is_valid(dict));
        wy_cbor_span value;
        REQUIRE_EQ(wy_cbor_map_find(span_of(dict), "a", &value), WY_ERR_NONE);
        wy_u32 u;
        CHECK_EQ(wy_cbor_get_u32(value, &u), WY_ERR_NONE);
        CHECK_EQ(u, 1u);
        // The symbol key is not a text key, so a text lookup of "x" misses.
        CHECK_EQ(wy_cbor_map_find(span_of(dict), "x", &value), WY_ERR_KEY);
        CHECK_EQ(wy_cbor_map_find(span_of(dict), "missing", &value), WY_ERR_KEY);

        REQUIRE_EQ(wy_cbor_open_map(span_of(dict), &reader, &count), WY_ERR_NONE);
        CHECK_EQ(count, 2u);
        wy_cbor_span key, val;
        REQUIRE_EQ(wy_cbor_next_span(&reader, &key), WY_ERR_NONE);
        REQUIRE_EQ(wy_cbor_next_span(&reader, &val), WY_ERR_NONE);
        REQUIRE_EQ(wy_cbor_next_span(&reader, &key), WY_ERR_NONE);
        REQUIRE_EQ(wy_cbor_next_span(&reader, &val), WY_ERR_NONE);
        wy_i32 neg;
        CHECK_EQ(wy_cbor_get_i32(val, &neg), WY_ERR_NONE);
        CHECK_EQ(neg, -2);
        CHECK(wy_cbor_reader_at_end(&reader));
    }

    TEST_CASE("never emitted: non-shortest arguments")
    {
        CHECK_FALSE(is_valid({0x18, 0x03}));
        CHECK_FALSE(is_valid({0x19, 0x00, 0x18}));
        CHECK_FALSE(is_valid({0x1A, 0x00, 0x00, 0xFF, 0xFF}));
        CHECK_FALSE(is_valid({0x1B, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF}));
        CHECK_FALSE(is_valid({0x38, 0x00}));  // negative, non-shortest
        // The longest value one form can hold is fine.
        CHECK(is_valid({0x18, 0x18}));
        CHECK(is_valid({0x19, 0x01, 0x00}));
        CHECK(is_valid({0x1A, 0x00, 0x01, 0x00, 0x00}));
        CHECK(is_valid({0x1B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}));
        // B8 05 ... must be A5 ...
        Bytes b8 = {0xB8, 0x05};
        for (int i = 0; i < 5; i++) { b8.push_back(0x61 + i); b8.push_back(0x00); }
        CHECK_FALSE(is_valid(b8));
        Bytes a5 = {0xA5};
        for (int i = 0; i < 5; i++) { a5.push_back(0x61); a5.push_back((std::uint8_t) ('a' + i)); a5.push_back(0x00); }
        // (the keys above repeat "a" prefixes; only the shape matters here)
        CHECK_FALSE(is_valid({0x58, 0x02, 0x01, 0x02}));  // byte string, non-shortest length
        CHECK_FALSE(is_valid({0x78, 0x00}));
        CHECK_FALSE(is_valid({0x98, 0x00}));
    }

    TEST_CASE("never emitted: indefinite lengths and the break byte")
    {
        CHECK_FALSE(is_valid({0x9F, 0x01, 0x02, 0xFF}));
        CHECK_FALSE(is_valid({0xBF, 0xFF}));
        CHECK_FALSE(is_valid({0x5F, 0xFF}));
        CHECK_FALSE(is_valid({0x7F, 0xFF}));
        CHECK_FALSE(is_valid({0xFF}));
    }

    TEST_CASE("never emitted: major 7 other than false, true, nil, float32, float64")
    {
        CHECK_FALSE(is_valid({0xF9, 0x3E, 0x00}));   // float16
        CHECK_FALSE(is_valid({0xF7}));               // undefined
        CHECK_FALSE(is_valid({0xF8, 0x20}));         // simple(32)
        CHECK_FALSE(is_valid({0xE0}));               // simple(0)
        CHECK_FALSE(is_valid({0xF3}));
        for (int b = 0xFC; b <= 0xFE; b++) { CHECK_FALSE(is_valid({(std::uint8_t) b})); }
    }

    TEST_CASE("never emitted: arguments 28 to 31")
    {
        for (int major = 0; major < 7; major++) {
            for (int arg = 28; arg <= 30; arg++) {
                CAPTURE(major);
                CAPTURE(arg);
                CHECK_FALSE(is_valid({(std::uint8_t) ((major << 5) | arg)}));
            }
        }
    }

    TEST_CASE("never emitted: 64-bit lengths and counts")
    {
        CHECK_FALSE(is_valid({0x5B, 0, 0, 0, 0, 0, 0, 0, 0}));
        CHECK_FALSE(is_valid({0x7B, 0, 0, 0, 0, 0, 0, 0, 0}));
        CHECK_FALSE(is_valid({0x9B, 0, 0, 0, 0, 0, 0, 0, 0}));
        CHECK_FALSE(is_valid({0xBB, 0, 0, 0, 0, 0, 0, 0, 0}));
    }

    TEST_CASE("never emitted: tags outside the subset")
    {
        CHECK_FALSE(is_valid({0xC0, 0x61, 0x78}));         // tag 0, date/time
        CHECK_FALSE(is_valid({0xD7, 0x01}));               // tag 23
        CHECK_FALSE(is_valid({0xD8, 0x1C, 0x01}));         // tag 28 is the protocol layer's
        CHECK_FALSE(is_valid({0xD8, 0x1D, 0x01}));         // tag 29
        CHECK_FALSE(is_valid({0xDA, 0x00, 0x01, 0x01, 0x18, 0x61, 0x78}));  // 4-byte tag number
        CHECK_FALSE(is_valid({0xDB, 0, 0, 0, 0, 0, 0, 0x01, 0x18, 0x61, 0x78}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x19, 0x61, 0x78}));  // tag 281
        // Tag 280 wraps text only, tag 282 an unsigned integer only, and
        // tags do not nest.
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x18, 0x01}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x18, 0x41, 0x78}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x1A, 0x61, 0x78}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x1A, 0x20}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x18, 0xD9, 0x01, 0x18, 0x61, 0x78}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x18}));         // nothing to wrap
        // A char is any u32, no more.
        CHECK(is_valid({0xD9, 0x01, 0x1A, 0x1A, 0xFF, 0xFF, 0xFF, 0xFF}));
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x1A, 0x1B, 0, 0, 0, 1, 0, 0, 0, 0}));
    }

    TEST_CASE("never emitted: a container as a map key, and other bad keys")
    {
        CHECK_FALSE(is_valid({0xA1, 0x81, 0x01, 0x02}));   // list
        CHECK_FALSE(is_valid({0xA1, 0xA0, 0x02}));         // map
        CHECK_FALSE(is_valid({0xA1, 0xF5, 0x02}));         // bool
        CHECK_FALSE(is_valid({0xA1, 0xF6, 0x02}));         // nil
        CHECK_FALSE(is_valid({0xA1, 0xFB, 0x3F, 0xF8, 0, 0, 0, 0, 0, 0, 0x02}));  // float
        CHECK_FALSE(is_valid({0xA1, 0xFA, 0x3F, 0xC0, 0, 0, 0x02}));
        CHECK_FALSE(is_valid({0xA1, 0xD9, 0x01, 0x1A, 0x18, 0x61, 0x02}));  // char
        // Integer, string, symbol and bytes keys are fine, and a container
        // is fine as a value.
        CHECK(is_valid({0xA1, 0x01, 0x81, 0x02}));
        CHECK(is_valid({0xA1, 0x20, 0x02}));
        CHECK(is_valid({0xA1, 0x61, 0x61, 0xA0}));
        CHECK(is_valid({0xA1, 0xD9, 0x01, 0x18, 0x61, 0x78, 0x02}));
        CHECK(is_valid({0xA1, 0x41, 0x01, 0x02}));
        // The check applies at every depth, and to the key position only.
        CHECK_FALSE(is_valid({0x81, 0xA1, 0x80, 0x01}));
        CHECK(is_valid({0xA2, 0x01, 0xF5, 0x02, 0xF6}));
    }

    TEST_CASE("never emitted: a trailing byte after the top-level item")
    {
        CHECK_FALSE(is_valid({0x61, 0x78, 0x00}));
        CHECK(is_valid({0x61, 0x78}));
        // The reader itself stops after one item.
        Bytes b = {0x61, 0x78, 0x00};
        wy_cbor_reader reader;
        wy_cbor_reader_init(&reader, b.data(), b.size());
        CHECK_EQ(wy_cbor_skip(&reader), WY_ERR_NONE);
        CHECK_FALSE(wy_cbor_reader_at_end(&reader));
    }

    TEST_CASE("truncated input is rejected at every length")
    {
        Bytes whole = wy_test_cbor::map({
            wy_test_cbor::kv("n", wy_test_cbor::text("hello")),
            wy_test_cbor::kv("l", wy_test_cbor::array({wy_test_cbor::uval(1), wy_test_cbor::float64(2.5), wy_test_cbor::nil()})),
            wy_test_cbor::kv("b", wy_test_cbor::byte_string({1, 2, 3})),
            wy_test_cbor::kv("s", wy_test_cbor::symbol("sym")),
            wy_test_cbor::kv("c", wy_test_cbor::character(0x1F600)),
        });
        REQUIRE(is_valid(whole));
        for (std::size_t len = 0; len < whole.size(); len++) {
            CAPTURE(len);
            CHECK_EQ(wy_cbor_validate(whole.data(), len), WY_ERR_IMAGE);
        }
    }

    TEST_CASE("a count or length the buffer cannot hold is rejected up front")
    {
        CHECK_FALSE(is_valid({0x9A, 0xFF, 0xFF, 0xFF, 0xFF}));
        CHECK_FALSE(is_valid({0xBA, 0xFF, 0xFF, 0xFF, 0xFF}));
        CHECK_FALSE(is_valid({0x5A, 0xFF, 0xFF, 0xFF, 0xFF, 0x00}));
        CHECK_FALSE(is_valid({0x82, 0x01}));
        CHECK_FALSE(is_valid({0xA1, 0x01}));
        // A map of n pairs needs 2n items: one byte short.
        CHECK_FALSE(is_valid({0xA2, 0x01, 0x02, 0x03}));
        wy_cbor_item it;
        CHECK_EQ(read_one({0x84, 0x01, 0x02}, &it), WY_ERR_IMAGE);
    }

    TEST_CASE("text must be valid UTF-8")
    {
        CHECK(is_valid({0x62, 0xC3, 0xA9}));                     // U+00E9
        CHECK(is_valid({0x63, 0xE2, 0x82, 0xAC}));               // U+20AC
        CHECK(is_valid({0x64, 0xF0, 0x9F, 0x98, 0x80}));         // U+1F600
        CHECK(is_valid({0x64, 0xF4, 0x8F, 0xBF, 0xBF}));         // U+10FFFF
        CHECK(is_valid({0x63, 0xED, 0x9F, 0xBF}));               // U+D7FF
        CHECK(is_valid({0x61, 0x00}));                           // NUL is valid text
        CHECK_FALSE(is_valid({0x62, 0xC0, 0x80}));               // overlong NUL
        CHECK_FALSE(is_valid({0x62, 0xC1, 0xBF}));               // overlong
        CHECK_FALSE(is_valid({0x63, 0xE0, 0x80, 0x80}));         // overlong 3-byte
        CHECK_FALSE(is_valid({0x64, 0xF0, 0x80, 0x80, 0x80}));   // overlong 4-byte
        CHECK_FALSE(is_valid({0x63, 0xED, 0xA0, 0x80}));         // surrogate U+D800
        CHECK_FALSE(is_valid({0x63, 0xED, 0xBF, 0xBF}));         // surrogate U+DFFF
        CHECK_FALSE(is_valid({0x64, 0xF4, 0x90, 0x80, 0x80}));   // above U+10FFFF
        CHECK_FALSE(is_valid({0x64, 0xF5, 0x80, 0x80, 0x80}));
        CHECK_FALSE(is_valid({0x61, 0x80}));                     // stray continuation
        CHECK_FALSE(is_valid({0x61, 0xC3}));                     // truncated sequence
        CHECK_FALSE(is_valid({0x62, 0xC3, 0x28}));               // bad continuation
        CHECK_FALSE(is_valid({0x61, 0xFF}));
        // Symbols are text too; byte strings are not checked.
        CHECK_FALSE(is_valid({0xD9, 0x01, 0x18, 0x62, 0xC0, 0x80}));
        CHECK(is_valid({0x42, 0xC0, 0x80}));
    }

    TEST_CASE("nesting stops at WY_CBOR_MAX_DEPTH")
    {
        // n nested one-element arrays around a 0.
        auto nested = [](int n) {
            Bytes b(n, 0x81);
            b.push_back(0x00);
            return b;
        };
        CHECK(is_valid(nested(WY_CBOR_MAX_DEPTH)));
        CHECK_FALSE(is_valid(nested(WY_CBOR_MAX_DEPTH + 1)));
        // The same through a map's values.
        Bytes m;
        for (int i = 0; i < WY_CBOR_MAX_DEPTH + 1; i++) { m.push_back(0xA1); m.push_back(0x00); }
        m.push_back(0x00);
        CHECK_FALSE(is_valid(m));
        // Empty containers take no level.
        Bytes e(WY_CBOR_MAX_DEPTH, 0x81);
        e.push_back(0x80);
        CHECK(is_valid(e));
        // A very deep input is rejected, not recursed into.
        CHECK_FALSE(is_valid(nested(100000)));
    }

    TEST_CASE("typed getters check type and range")
    {
        using namespace wy_test_cbor;
        wy_u32 u32;
        wy_i32 i32;
        wy_u64 u64;
        wy_i64 i64;
        bool flag;
        const char* str;
        const wy_u8* data;
        wy_uword len;

        Bytes big = uval(0x100000000ull);
        CHECK_EQ(wy_cbor_get_u32(span_of(big), &u32), WY_ERR_IMAGE);
        CHECK_EQ(wy_cbor_get_u64(span_of(big), &u64), WY_ERR_NONE);
        CHECK_EQ(u64, 0x100000000ull);
        Bytes max32 = uval(0xFFFFFFFFu);
        CHECK_EQ(wy_cbor_get_u32(span_of(max32), &u32), WY_ERR_NONE);
        CHECK_EQ(u32, 0xFFFFFFFFu);
        CHECK_EQ(wy_cbor_get_i32(span_of(max32), &i32), WY_ERR_IMAGE);
        Bytes int32_max = integer(INT32_MAX);
        Bytes int32_min = integer(INT32_MIN);
        Bytes below = integer((std::int64_t) INT32_MIN - 1);
        CHECK_EQ(wy_cbor_get_i32(span_of(int32_max), &i32), WY_ERR_NONE);
        CHECK_EQ(i32, INT32_MAX);
        CHECK_EQ(wy_cbor_get_i32(span_of(int32_min), &i32), WY_ERR_NONE);
        CHECK_EQ(i32, INT32_MIN);
        CHECK_EQ(wy_cbor_get_i32(span_of(below), &i32), WY_ERR_IMAGE);
        CHECK_EQ(wy_cbor_get_i64(span_of(below), &i64), WY_ERR_NONE);
        // An unsigned getter refuses a negative, and an integer getter a float.
        Bytes neg = integer(-1);
        CHECK_EQ(wy_cbor_get_u32(span_of(neg), &u32), WY_ERR_IMAGE);
        CHECK_EQ(wy_cbor_get_u64(span_of(neg), &u64), WY_ERR_IMAGE);
        Bytes f = float64(1.0);
        CHECK_EQ(wy_cbor_get_i32(span_of(f), &i32), WY_ERR_IMAGE);
        CHECK_EQ(wy_cbor_get_u32(span_of(f), &u32), WY_ERR_IMAGE);
        Bytes huge = uval(0x8000000000000000ull);
        CHECK_EQ(wy_cbor_get_i64(span_of(huge), &i64), WY_ERR_IMAGE);

        Bytes t = boolean(true), nf = boolean(false), n = nil();
        CHECK_EQ(wy_cbor_get_bool(span_of(t), &flag), WY_ERR_NONE);
        CHECK(flag);
        CHECK_EQ(wy_cbor_get_bool(span_of(nf), &flag), WY_ERR_NONE);
        CHECK_FALSE(flag);
        CHECK_EQ(wy_cbor_get_bool(span_of(n), &flag), WY_ERR_IMAGE);

        Bytes s = text("abc"), by = byte_string({9, 8});
        CHECK_EQ(wy_cbor_get_text(span_of(s), &str, &len), WY_ERR_NONE);
        CHECK_EQ(std::string(str, len), "abc");
        CHECK_EQ(wy_cbor_get_text(span_of(by), &str, &len), WY_ERR_IMAGE);
        CHECK_EQ(wy_cbor_get_bytes(span_of(by), &data, &len), WY_ERR_NONE);
        CHECK_EQ(len, 2u);
        CHECK_EQ(data[0], 9);
        CHECK_EQ(wy_cbor_get_bytes(span_of(s), &data, &len), WY_ERR_IMAGE);
        // Symbols are not strings: a getter wants the type it names.
        Bytes sym = symbol("x");
        CHECK_EQ(wy_cbor_get_text(span_of(sym), &str, &len), WY_ERR_IMAGE);
    }

    TEST_CASE("containers open only as their own type")
    {
        using namespace wy_test_cbor;
        Bytes arr = array({uval(1)});
        Bytes m = map({kv("a", uval(1))});
        wy_cbor_reader reader;
        wy_uword count;
        CHECK_EQ(wy_cbor_open_array(span_of(m), &reader, &count), WY_ERR_IMAGE);
        CHECK_EQ(wy_cbor_open_map(span_of(arr), &reader, &count), WY_ERR_IMAGE);
        wy_cbor_span value;
        CHECK_EQ(wy_cbor_map_find(span_of(arr), "a", &value), WY_ERR_IMAGE);
        Bytes empty_map = map({});
        CHECK_EQ(wy_cbor_map_find(span_of(empty_map), "a", &value), WY_ERR_KEY);
        CHECK_EQ(wy_cbor_open_array(wy_cbor_span{nullptr, 0}, &reader, &count), WY_ERR_IMAGE);
        Bytes empty_arr = array({});
        REQUIRE_EQ(wy_cbor_open_array(span_of(empty_arr), &reader, &count), WY_ERR_NONE);
        CHECK_EQ(count, 0u);
    }

    TEST_CASE("map lookup uses the exact text key")
    {
        using namespace wy_test_cbor;
        Bytes m = map({kv("sl", uval(1)), kv("s", uval(2)), Pair{uval(7), uval(3)}, Pair{byte_string({'s'}), uval(4)}});
        wy_cbor_span value;
        wy_u32 v;
        REQUIRE_EQ(wy_cbor_map_find(span_of(m), "s", &value), WY_ERR_NONE);
        REQUIRE_EQ(wy_cbor_get_u32(value, &v), WY_ERR_NONE);
        CHECK_EQ(v, 2u);
        REQUIRE_EQ(wy_cbor_map_find(span_of(m), "sl", &value), WY_ERR_NONE);
        REQUIRE_EQ(wy_cbor_get_u32(value, &v), WY_ERR_NONE);
        CHECK_EQ(v, 1u);
        CHECK_EQ(wy_cbor_map_find(span_of(m), "", &value), WY_ERR_KEY);
        CHECK_EQ(wy_cbor_map_find(span_of(m), "slx", &value), WY_ERR_KEY);
    }
}
