#include <doctest/doctest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <wyrm/image_loader.h>

// pypoc/doc/wyc-format.md §2 and Appendix B step 1. Fixtures are the
// committed test/bytecode/hello.wyc (WY_TEST_HELLO_WYC_PATH, from
// src/test/meson.build) with single-byte mutations, the same approach
// pypoc/test/test_vm_load.py uses against the same file.

namespace
{

std::vector<wy_u8> read_hello_wyc()
{
    std::ifstream f(WY_TEST_HELLO_WYC_PATH, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "could not open " WY_TEST_HELLO_WYC_PATH);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    return std::vector<wy_u8>(s.begin(), s.end());
}

wy_u32 read_u32_le(const wy_u8* p)
{
    return (wy_u32) p[0] | ((wy_u32) p[1] << 8) | ((wy_u32) p[2] << 16) | ((wy_u32) p[3] << 24);
}

void write_u32_le(wy_u8* p, wy_u32 v)
{
    p[0] = (wy_u8) (v & 0xff);
    p[1] = (wy_u8) ((v >> 8) & 0xff);
    p[2] = (wy_u8) ((v >> 16) & 0xff);
    p[3] = (wy_u8) ((v >> 24) & 0xff);
}

}  // namespace

TEST_SUITE("image")
{
    TEST_CASE("hello.wyc loads and every section lands at its id")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        wy_module_image image;

        REQUIRE_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_NONE);

        // hello.wyc has header, statics, functions, code, debug, exports,
        // free (ids 1,2,5,8,9,10,11) and nothing else (no slot_defaults,
        // symbols, classes or messages).
        CHECK_NE(image.sections[WY_SEC_HEADER].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_STATICS].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_SLOT_DEFAULTS].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_SYMBOLS].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_FUNCTIONS].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_CLASSES].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_MESSAGES].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_CODE].data, nullptr);
        CHECK_EQ(image.sections[WY_SEC_CODE].len % 4, 0);
        CHECK_NE(image.sections[WY_SEC_DEBUG].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_EXPORTS].data, nullptr);
        CHECK_NE(image.sections[WY_SEC_FREE].data, nullptr);
    }

    TEST_CASE("rejects a file shorter than the container header")
    {
        std::vector<wy_u8> blob = {'W', 'Y', 'C'};
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects bad magic")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        blob[0] = 'E';
        blob[1] = 'L';
        blob[2] = 'F';
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a future container version")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        blob[4] = 2;
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects an unknown section id")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        blob[8] = 99;  // first directory entry's id
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects an unsorted directory")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        // Swap the first two 12-byte directory entries.
        wy_u8 first[12];
        std::memcpy(first, &blob[8], 12);
        std::memcpy(&blob[8], &blob[20], 12);
        std::memcpy(&blob[20], first, 12);
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a duplicate section id")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        blob[20] = blob[8];  // second entry claims the first's id
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a section running past the end of the file")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        write_u32_le(&blob[16], 1u << 20);  // first entry's length
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a section overlapping the directory")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        write_u32_le(&blob[12], 4);  // first entry's offset, inside the header
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a payload that is not 4-byte aligned")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        wy_u32 offset = read_u32_le(&blob[12]);  // first entry's offset
        write_u32_le(&blob[12], offset + 1);
        write_u32_le(&blob[16], read_u32_le(&blob[16]) - 1);  // keep it in bounds
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a code payload whose length is not a multiple of 4")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        // Find the code (id 8) directory entry and shrink its length by 1
        // (code sections are 4-byte aligned, so length-1 is never a
        // multiple of 4, and offset stays aligned).
        wy_u8 count = blob[5];
        bool found = false;
        for (wy_u8 i = 0; i < count; i++) {
            wy_uword entry = 8 + (wy_uword) i * 12;
            if (blob[entry] == WY_SEC_CODE) {
                write_u32_le(&blob[entry + 8], read_u32_le(&blob[entry + 8]) - 1);
                found = true;
                break;
            }
        }
        REQUIRE(found);
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), &image), WY_ERR_IMAGE);
    }

    TEST_CASE("rejects a null data or out pointer")
    {
        std::vector<wy_u8> blob = read_hello_wyc();
        wy_module_image image;
        CHECK_EQ(wy_image_from_bytes(nullptr, blob.size(), &image), WY_ERR_INVAL);
        CHECK_EQ(wy_image_from_bytes(blob.data(), blob.size(), nullptr), WY_ERR_INVAL);
    }
}
