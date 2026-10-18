#include <doctest/doctest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <wyrm/cbor.h>
#include <wyrm/image_loader.h>

#include <test_common/cbor_build.h>

// The .wyc container (doc-llm/llm-bytecode.md section 4): the 11-byte
// preamble, then one map from section id to value. Fixtures are the
// hello.wyd, built from test/corpus/hello.wy by this build tree's compiler
// (WY_TEST_FIXTURE_DIR, from src/test/meson.build), with single-byte
// mutations, plus images built by hand.

namespace
{

using namespace wy_test_cbor;

std::vector<wy_u8> read_hello_wyd()
{
    std::ifstream f(WY_TEST_FIXTURE_DIR "/hello.wyd", std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "could not open " WY_TEST_FIXTURE_DIR "/hello.wyd");
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    return std::vector<wy_u8>(s.begin(), s.end());
}

Bytes header_section() { return map({kv("n", text("m")), kv("g", uval(0)), kv("l", uval(0))}); }

// The smallest valid file: a header and an (empty) code section.
std::vector<Section> minimal_sections()
{
    return {{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, code_item({7, 8})}};
}

wy_error parse(const Bytes& blob, wy_module_image* image)
{
    return wy_image_from_bytes(blob.data(), blob.size(), image);
}

}  // namespace

TEST_SUITE("image")
{
    TEST_CASE("hello.wyd starts with the preamble and every section lands at its id")
    {
        std::vector<wy_u8> blob = read_hello_wyd();
        REQUIRE_GT(blob.size(), 11u);
        Bytes pre = preamble();
        CHECK(std::equal(pre.begin(), pre.end(), blob.begin()));
        CHECK_EQ(WY_IMAGE_VERSION, 340758572u);
        wy_module_image image;

        REQUIRE_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_NONE);

        // The sections every module has. Which optional ones appear (slot
        // defaults, symbols, classes, messages, debug) is up to the compiler
        // and the source, so they are not asserted here.
        CHECK_NE(image.sections[WY_SEC_HEADER].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_STATICS].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_FUNCTIONS].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_CODE].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_CODE].len % 4, 0);
        CHECK_NE(image.sections[WY_SEC_EXPORTS].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_FREE].data, nullptr);
    }

    TEST_CASE("a minimal image parses and its sections point into the file")
    {
        Bytes blob = container(minimal_sections());
        wy_module_image image;
        REQUIRE_EQ(parse(blob, &image), WY_ERR_NONE);
        // A structured section is the item itself ...
        CHECK_EQ(image.sections[WY_SEC_HEADER].len, header_section().size());
        CHECK_EQ(image.sections[WY_SEC_HEADER].data[0], 0xA3);
        CHECK_GE(image.sections[WY_SEC_HEADER].data, blob.data() + 11);
        // ... and code is the words inside its byte string.
        CHECK_EQ(image.sections[WY_SEC_CODE].len, 8u);
        CHECK_EQ(image.sections[WY_SEC_CODE].data[0], 7);
        CHECK_EQ(image.sections[WY_SEC_CODE].data[4], 8);
        CHECK_EQ(image.sections[WY_SEC_STATICS].data, nullptr);
        CHECK_EQ(image.name, nullptr);
    }

    TEST_CASE("an empty code section is present")
    {
        Bytes blob = container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, code_item({})}});
        wy_module_image image;
        REQUIRE_EQ(parse(blob, &image), WY_ERR_NONE);
        CHECK_NE(image.sections[WY_SEC_CODE].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_CODE].len, 0u);
    }

    TEST_CASE("rejects a file shorter than the preamble")
    {
        Bytes pre = preamble();
        for (std::size_t len = 0; len < pre.size(); len++) {
            CAPTURE(len);
            wy_module_image image;
            CHECK_EQ(wy_image_from_bytes(pre.data(), len, &image), WY_ERR_IMAGE);
        }
    }

    TEST_CASE("rejects a preamble with nothing after it")
    {
        Bytes blob = preamble();
        wy_module_image image;
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects wrong magic, one byte at a time")
    {
        for (std::size_t i = 0; i < 7; i++) {
            CAPTURE(i);
            Bytes blob = container(minimal_sections());
            blob[i] ^= 0x01;
            wy_module_image image;
            CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        }
    }

    TEST_CASE("rejects bad magic from another format")
    {
        Bytes blob = container(minimal_sections());
        blob[0] = 'E'; blob[1] = 'L'; blob[2] = 'F';
        wy_module_image image;
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("accepts exactly its own version")
    {
        wy_module_image image;
        CHECK_EQ(parse(container(minimal_sections(), WY_IMAGE_VERSION), &image), WY_ERR_NONE);
        CHECK_EQ(parse(container(minimal_sections(), WY_IMAGE_VERSION - 1), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container(minimal_sections(), WY_IMAGE_VERSION + 1), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container(minimal_sections(), 0), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container(minimal_sections(), 1), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container(minimal_sections(), 0xFFFFFFFFu), &image), WY_ERR_IMAGE);
        // Each byte of the version counts.
        for (std::size_t i = 7; i < 11; i++) {
            CAPTURE(i);
            Bytes blob = container(minimal_sections());
            blob[i] ^= 0x80;
            CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        }
    }

    TEST_CASE("rejects a file in the old BSON container")
    {
        // "WYC\0", version 1, a section count, then the directory.
        Bytes old = {'W', 'Y', 'C', 0, 1, 2, 0, 0};
        Bytes entry_header = {WY_SEC_HEADER, 0, 0, 0, 32, 0, 0, 0, 5, 0, 0, 0};
        Bytes entry_code = {WY_SEC_CODE, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0};
        old.insert(old.end(), entry_header.begin(), entry_header.end());
        old.insert(old.end(), entry_code.begin(), entry_code.end());
        old.resize(48, 0);
        wy_module_image image;
        CHECK_EQ(parse(old, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a preamble that is not canonical")
    {
        wy_module_image image;
        // array(2) with a 1-byte count, an indefinite array, text in 2 bytes.
        Bytes body = container(minimal_sections());
        Bytes tail(body.begin() + 11, body.end());
        Bytes pre = preamble();
        Bytes wide_count = {0x98, 0x02};
        wide_count.insert(wide_count.end(), pre.begin() + 1, pre.end());
        wide_count.insert(wide_count.end(), tail.begin(), tail.end());
        CHECK_EQ(parse(wide_count, &image), WY_ERR_IMAGE);
        Bytes wide_version = {0x82, 0x64, 'W', 'Y', 'C', 'R', 0x1B, 0, 0, 0, 0, 0x14, 0x4F, 0x46, 0x60};
        wide_version.insert(wide_version.end(), tail.begin(), tail.end());
        CHECK_EQ(parse(wide_version, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects bytes after the section map")
    {
        wy_module_image image;
        Bytes blob = container(minimal_sections());
        blob.push_back(0x00);
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        blob = container(minimal_sections());
        blob.insert(blob.end(), 11, 0x00);
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        // A second item, even a valid one, is not part of the file.
        blob = container(minimal_sections());
        Bytes extra = container(minimal_sections());
        blob.insert(blob.end(), extra.begin(), extra.end());
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a truncated file at every length")
    {
        Bytes blob = container({
            {WY_SEC_HEADER, header_section()},
            {WY_SEC_STATICS, array({text("a"), uval(1)})},
            {WY_SEC_CODE, code_item({1, 2, 3})},
        });
        wy_module_image image;
        REQUIRE_EQ(parse(blob, &image), WY_ERR_NONE);
        for (std::size_t len = 0; len < blob.size(); len++) {
            CAPTURE(len);
            CHECK_EQ(wy_image_from_bytes(blob.data(), len, &image), WY_ERR_IMAGE);
        }
    }

    TEST_CASE("rejects an unknown section id")
    {
        wy_module_image image;
        for (std::uint8_t id : {0, 12, 23, 99}) {
            CAPTURE((int) id);
            CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, code_item({})}, {id, map({})}}), &image), WY_ERR_IMAGE);
        }
        // An id that is not an unsigned integer.
        Bytes blob = container(minimal_sections());
        Bytes bad = preamble();
        Bytes h = head(5, 1);
        bad.insert(bad.end(), h.begin(), h.end());
        Bytes key = text("a");
        bad.insert(bad.end(), key.begin(), key.end());
        Bytes val = map({});
        bad.insert(bad.end(), val.begin(), val.end());
        CHECK_EQ(parse(bad, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects descending section ids")
    {
        wy_module_image image;
        Bytes blob = container({{WY_SEC_CODE, code_item({})}, {WY_SEC_HEADER, header_section()}});
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        blob = container({{WY_SEC_HEADER, header_section()}, {WY_SEC_SYMBOLS, array({})}, {WY_SEC_STATICS, array({})}, {WY_SEC_CODE, code_item({})}});
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a duplicate section id")
    {
        wy_module_image image;
        Bytes blob = container({{WY_SEC_HEADER, header_section()}, {WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, code_item({})}});
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        blob = container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, code_item({})}, {WY_SEC_CODE, code_item({})}});
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a missing header or code section")
    {
        wy_module_image image;
        CHECK_EQ(parse(container({{WY_SEC_CODE, code_item({})}}), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}}), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container({}), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a section of the wrong type")
    {
        wy_module_image image;
        // header must be a map, statics an array, code a byte string.
        CHECK_EQ(parse(container({{WY_SEC_HEADER, array({})}, {WY_SEC_CODE, code_item({})}}), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_STATICS, map({})}, {WY_SEC_CODE, code_item({})}}), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_EXPORTS, array({})}, {WY_SEC_CODE, code_item({})}}), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, array({uval(1)})}}), &image), WY_ERR_IMAGE);
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, text("abcd")}}), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a code payload whose length is not a multiple of 4")
    {
        wy_module_image image;
        for (std::size_t n : {1u, 2u, 3u, 5u, 7u}) {
            CAPTURE(n);
            CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, byte_string(Bytes(n, 0))}}), &image), WY_ERR_IMAGE);
        }
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_CODE, byte_string(Bytes(8, 0))}}), &image), WY_ERR_NONE);
    }

    TEST_CASE("rejects a section holding something outside the wire subset")
    {
        wy_module_image image;
        // A float16, an indefinite array, a non-shortest integer, bad UTF-8, a
        // list as a map key: each makes the whole file invalid, wherever it sits.
        const std::vector<Bytes> bad_items = {
            {0xF9, 0x3E, 0x00}, {0x9F, 0xFF}, {0x18, 0x03}, {0x62, 0xC0, 0x80}, {0xA1, 0x80, 0x01},
        };
        for (const Bytes& bad : bad_items) {
            Bytes statics = head(4, 1);
            statics.insert(statics.end(), bad.begin(), bad.end());
            CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_STATICS, statics}, {WY_SEC_CODE, code_item({})}}), &image), WY_ERR_IMAGE);
        }
        // A section nested past the depth limit.
        Bytes deep(WY_CBOR_MAX_DEPTH + 1, 0x81);
        deep.push_back(0x00);
        Bytes statics = head(4, 1);
        statics.insert(statics.end(), deep.begin(), deep.end());
        CHECK_EQ(parse(container({{WY_SEC_HEADER, header_section()}, {WY_SEC_STATICS, statics}, {WY_SEC_CODE, code_item({})}}), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a map that declares more sections than it holds")
    {
        wy_module_image image;
        Bytes blob = container(minimal_sections());
        blob[11] += 1;   // A2 -> A3
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
        blob = container(minimal_sections());
        blob[11] -= 1;   // A2 -> A1: the second pair is now trailing bytes
        CHECK_EQ(parse(blob, &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a null data or out pointer")
    {
        Bytes blob = container(minimal_sections());
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(nullptr, blob.size(), &image), WY_ERR_INVAL);
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), nullptr), WY_ERR_INVAL);
    }
}
