#include <doctest/doctest.h>

#include <fstream>
#include <filesystem>
#include <string>

#include <unistd.h>

#include <wyrm.h>
#include <wyrm/platform/hosted/import_fs.h>
#include <test_common/test_fiber_fixture.h>

/**
 * Hosted filesystem import hook (epic 5 M2): `a::b` -> `<root>/a/b.wyc`,
 * root order = search order, first hit wins, no `.wy` source fallback.
 * Any bytes at the resolved path round-trip so the tests need no compiler.
 */

namespace {

struct temp_dir
{
    std::filesystem::path root;

    temp_dir()
    {
        root = std::filesystem::temp_directory_path() /
            ("wyrm_import_fs_test_" + std::to_string(static_cast<long>(::getpid())) +
             "_" + std::to_string(++counter));
        std::filesystem::create_directories(root);
    }

    ~temp_dir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    static int counter;
};

int temp_dir::counter = 0;

void write_file(const std::filesystem::path& path, const std::string& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "could not write ", path.string());
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE_MESSAGE(f.good(), "short write to ", path.string());
}

std::string read_out(const wy_u8* bytes, wy_uword len)
{
    return std::string(reinterpret_cast<const char*>(bytes), len);
}

} // namespace

TEST_SUITE("import_fs")
{
    TEST_CASE("hook resolves a::b to root/a/b.wyc and returns the image bytes") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        const std::string payload = "\x00\x01\x02not actually an image\xff";
        write_file(tmp.root / "a" / "b.wyc", payload);

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "a::b";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(len, payload.size());
        CHECK_EQ(read_out(bytes, len), payload);

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("hook handles nested modules a::b::c and multiple components") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        const std::string payload = "deep";
        write_file(tmp.root / "pkg" / "sub" / "leaf.wyc", payload);

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "pkg::sub::leaf";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), payload);

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("first root wins: module present under more than one -I root") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir first;
        temp_dir second;
        const std::string payload_first = "from first root";
        const std::string payload_second = "from second root";
        write_file(first.root / "a" / "b.wyc", payload_first);
        write_file(second.root / "a" / "b.wyc", payload_second);

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        // In the order the -I flags were given; the first match wins.
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            first.root.string().c_str()), WY_ERR_NONE);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            second.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "a::b";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), payload_first);

        // Reverse the order: the other root's image now wins.
        wy_import_fs_search_path search2;
        wy_import_fs_search_path_init_s(&search2);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search2,
            second.root.string().c_str()), WY_ERR_NONE);
        wy_u8* bytes2 = WY_NULL;
        wy_uword len2 = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes2, &len2, &search2), WY_ERR_NONE);
        REQUIRE_NE(bytes2, WY_NULL);
        CHECK_EQ(read_out(bytes2, len2), payload_second);

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search2);
    }

    TEST_CASE("a missing module under every root faults WY_ERR_UNBOUND") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "no::such::module";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &search), WY_ERR_UNBOUND);
        CHECK_EQ(bytes, WY_NULL);

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("an empty search path faults WY_ERR_UNBOUND even when the file exists") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        const std::string path = "a::b";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &search), WY_ERR_UNBOUND);
    }

    TEST_CASE("root strings survive the caller's buffer: duplication on add") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        write_file(tmp.root / "a" / "b.wyc", "ephemeral root buffer");

        std::string buffer(tmp.root.string().c_str(),
            tmp.root.string().size());  // a copy; the original argv string is gone
        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search, buffer.c_str()),
            WY_ERR_NONE);

        const std::string path = "a::b";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "ephemeral root buffer");

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("read_file returns the file bytes and WY_NULL for a missing file") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        const std::string content = "whole-file read";
        write_file(tmp.root / "entry.wyc", content);

        wy_uword size = 0;
        wy_u8* data = wy_import_fs_read_file(ctx, (tmp.root / "entry.wyc").string().c_str(), &size);
        REQUIRE_NE(data, WY_NULL);
        CHECK_EQ(size, content.size());
        CHECK_EQ(read_out(data, size), content);

        wy_uword missing_size = 0;
        CHECK_EQ(wy_import_fs_read_file(ctx, (tmp.root / "missing.wyc").string().c_str(), &missing_size), WY_NULL);
        CHECK(missing_size == 0);
    }
}