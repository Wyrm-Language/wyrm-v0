#include <iterator>
#include <doctest/doctest.h>
#include <wyrm/bson.h>
#include <wyrm/string.h>

static const uint8_t hello_1_header[] = {
    0x38, 0x00, 0x00, 0x00,

    /* n: "hello_1" */
    0x02, 0x6E, 0x00, 0x08, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x5F, 0x31, 0x00,

    /* v: 1 */
    0x10, 0x76, 0x00, 0x01, 0x00, 0x00, 0x00,

    /* g: 0 */
    0x10, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* l: 2 */
    0x10, 0x6C, 0x00, 0x02, 0x00, 0x00, 0x00,

    /* u: [0] */
    0x04, 0x75, 0x00,
        0x0C, 0x00, 0x00, 0x00,

        0x10, 0x30, 0x00,  0x00, 0x00, 0x00, 0x00, /* 0: 0 */
        0x00,  /* end u: doc */

    0x00, /* end bson */
};

// -- fixtures for §4.2 strictness: hand-built minimal documents, each with
// -- exactly one field, so the field under test is unambiguous.

// total_len=14; tag=string, key "s", declared length 100 but only 2 bytes of
// body follow - the document ends long before the field claims to.
static const uint8_t truncated_string_doc[] = {
    0x0E, 0x00, 0x00, 0x00,
    0x02, 0x73, 0x00, 0x64, 0x00, 0x00, 0x00, 0x41, 0x00,
    0x00,
};

// total_len=16; tag=0x11 (u64), not one of the eight permitted tags (§4.2).
static const uint8_t non_permitted_tag_doc[] = {
    0x10, 0x00, 0x00, 0x00,
    0x11, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
};

// total_len=9; tag=bool, value byte 0x05 - neither 0x00 nor 0x01.
static const uint8_t bad_bool_byte_doc[] = {
    0x09, 0x00, 0x00, 0x00,
    0x08, 0x62, 0x00, 0x05,
    0x00,
};

// total_len=15; tag=binary, declared length 2, subtype 1 (only 0 is permitted).
static const uint8_t bad_binary_subtype_doc[] = {
    0x0F, 0x00, 0x00, 0x00,
    0x05, 0x78, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0xAA, 0xBB,
    0x00,
};

// total_len=15; tag=double (8 bytes), key "d".
static const uint8_t double_field_doc[] = {
    0x0F, 0x00, 0x00, 0x00,
    0x01, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,  // 1.0
    0x00,
};

TEST_SUITE("bson")
{
    TEST_CASE("basic reading")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, hello_1_header, std::size(hello_1_header));

        auto err = wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len);
        REQUIRE_EQ(err, WY_ERR_NONE);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_STRING);

        const char* sname;
        wy_uword slen;
        wy_bson_get_str(buffer, &sname, &slen);

        REQUIRE_EQ(wy_memcmp(sname, "hello_1", slen), 0);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_STRING);
    }


    TEST_CASE("find and recurse")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, hello_1_header, std::size(hello_1_header));
        wy_bson_doc_reader_find(&dc, "u", &tag_type, &buffer, &buffer_len);

        REQUIRE_EQ(tag_type, WY_BSON_TAG_ARRAY);

        wy_bson_doc_reader arr_doc;
        wy_bson_doc_reader_start(&arr_doc, buffer, buffer_len);

        wy_bson_doc_reader_get(&arr_doc, &tag_type, &name, &buffer, &buffer_len);
        REQUIRE_EQ(wy_strcmp_f(name, "0"), 0);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_I32);
        REQUIRE_EQ(wy_bson_get_i32(buffer), 0);

        REQUIRE_EQ(wy_bson_doc_reader_get(&arr_doc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_STOP_ITERATION);
    }

    TEST_CASE("array reader ignores index keys")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, hello_1_header, std::size(hello_1_header));
        wy_bson_doc_reader_find(&dc, "u", &tag_type, &buffer, &buffer_len);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_ARRAY);

        wy_bson_array_reader arr;
        wy_bson_array_reader_start(&arr, buffer, buffer_len);
        REQUIRE_EQ(wy_bson_array_reader_get(&arr, &tag_type, &buffer, &buffer_len), WY_ERR_NONE);
        CHECK_EQ(tag_type, WY_BSON_TAG_I32);
        CHECK_EQ(wy_bson_get_i32(buffer), 0);
        CHECK_EQ(wy_bson_array_reader_get(&arr, &tag_type, &buffer, &buffer_len), WY_ERR_STOP_ITERATION);
    }

    TEST_CASE("reads a double field")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, double_field_doc, std::size(double_field_doc));
        REQUIRE_EQ(wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_NONE);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_DOUBLE);

        double value = 0.0;
        REQUIRE_EQ(wy_bson_get_double_f(buffer, buffer_len, &value), WY_ERR_NONE);
        CHECK_EQ(value, 1.0);
    }

    // -- §4.2 strictness: a reader MUST reject anything outside the eight
    // -- permitted tags, a bool byte that isn't 0/1, a binary subtype other
    // -- than 0, or a field that runs past the document's declared length.

    TEST_CASE("rejects a field that overruns the document")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, truncated_string_doc, std::size(truncated_string_doc));
        REQUIRE_EQ(wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a tag outside the permitted eight")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, non_permitted_tag_doc, std::size(non_permitted_tag_doc));
        REQUIRE_EQ(wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a bool byte that is neither 0 nor 1")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, bad_bool_byte_doc, std::size(bad_bool_byte_doc));
        REQUIRE_EQ(wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a binary subtype other than 0")
    {
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, bad_binary_subtype_doc, std::size(bad_binary_subtype_doc));
        REQUIRE_EQ(wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_IMAGE);
    }

    TEST_CASE("a 0x00 byte before the declared end is not a terminator")
    {
        // total_len=9, but the first field byte (offset 4) is already 0x00.
        // The real terminator per the declared length is offset 8, so this
        // must be rejected rather than read as an early, empty document.
        static const uint8_t early_zero_doc[] = {
            0x09, 0x00, 0x00, 0x00,
            0x00,
            0xAA, 0xAA, 0xAA, 0xAA,
        };
        wy_bson_doc_reader dc;
        wy_u8 tag_type;
        const wy_u8* buffer;
        const char* name;
        wy_uword buffer_len;

        wy_bson_doc_reader_start(&dc, early_zero_doc, std::size(early_zero_doc));
        REQUIRE_EQ(wy_bson_doc_reader_get(&dc, &tag_type, &name, &buffer, &buffer_len), WY_ERR_IMAGE);
    }
}
