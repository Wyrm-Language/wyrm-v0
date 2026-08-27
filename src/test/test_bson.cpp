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
        REQUIRE_EQ(err, WYRM_ERR_NONE);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_STRING);

        const char* sname;
        wy_uword slen;
        wy_bson_get_str(buffer, &sname, &slen);

        REQUIRE_EQ(wyrm_memcmp(sname, "hello_1", slen), 0);
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
        REQUIRE_EQ(wyrm_strcmp_f(name, "0"), 0);
        REQUIRE_EQ(tag_type, WY_BSON_TAG_I32);
        REQUIRE_EQ(wy_bson_get_i32(buffer), 0);

        REQUIRE_EQ(wy_bson_doc_reader_get(&arr_doc, &tag_type, &name, &buffer, &buffer_len), WYRM_ERR_STOP_ITERATION);
    }
}
