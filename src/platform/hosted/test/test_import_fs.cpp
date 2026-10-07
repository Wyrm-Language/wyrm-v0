#include <doctest/doctest.h>

#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>

#include <unistd.h>

#include <wyrm.h>
#include <wyrm/platform/hosted/import_cache.h>
#include <wyrm/platform/hosted/import_fs.h>
#include <test_common/test_fiber_fixture.h>

/**
 * Hosted filesystem import hook (epic 5 M2): `a::b` -> `<root>/a/b.wyd` then
 * `<root>/a/b.wyc`, root order = search order, first hit wins, `.wyd` before
 * `.wyc` in the same root, no `.wy` source fallback. The builtin module
 * table (epic 11) is consulted last and answers a static image. Any bytes at
 * a resolved path round-trip so the tests need no compiler.
 *
 * Packages (design/modules.md M1): within a root `<path>/__init__` beats
 * `<path>`, and a bare directory (or table rows filed under the path) is a
 * namespace package, answered with neither output set.
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
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
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
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
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
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), payload_first);

        // Reverse the order: the other root's image now wins.
        wy_import_fs_search_path search2;
        wy_import_fs_search_path_init_s(&search2);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search2,
            second.root.string().c_str()), WY_ERR_NONE);
        wy_u8* bytes2 = WY_NULL;
        wy_uword len2 = 0;
        const wy_module_image* image2 = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes2, &len2, &image2, &search2), WY_ERR_NONE);
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
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_UNBOUND);
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
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_UNBOUND);
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
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
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

    TEST_CASE("a .wyd shadows a .wyc in the same root; .wyc is found alone") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        write_file(tmp.root / "a" / "b.wyc", "from pypoc");
        write_file(tmp.root / "a" / "b.wyd", "from the port");

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "a::b";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "from the port");
        CHECK_EQ(image, WY_NULL);

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("the builtin table answers a static image when every root misses") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        static const wy_module_image dummy_image = { "dummy", {} };
        const wy_import_fs_builtin table[] = {
            { "std::thing", &dummy_image },
        };

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        search.builtins = table;
        search.builtin_count = 1;

        const std::string path = "std::thing";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        CHECK_EQ(bytes, WY_NULL);
        CHECK_EQ(len, 0u);
        REQUIRE_EQ(image, &dummy_image);

        // A name the table does not hold stays WY_ERR_UNBOUND.
        const std::string other = "std::other";
        CHECK_EQ(wy_import_fs_hook(ctx, other.data(), other.size(), &bytes, &len, &image, &search), WY_ERR_UNBOUND);
    }

    TEST_CASE("a root hit shadows the builtin table (table is the last resort)") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        write_file(tmp.root / "std" / "thing.wyc", "from disk");

        static const wy_module_image dummy_image = { "dummy", {} };
        const wy_import_fs_builtin table[] = {
            { "std::thing", &dummy_image },
        };

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);
        search.builtins = table;
        search.builtin_count = 1;

        const std::string path = "std::thing";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "from disk");
        CHECK_EQ(image, WY_NULL);

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("cache path mapping: __wycache__ default and --cache-dir prefix") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        wy_allocator* allocator = wy_context_get_machine(ctx)->allocator;
        REQUIRE_NE(allocator, nullptr);

        char* plain = wy_import_cache_path(allocator, nullptr, "/repo/pkg/mod.wy");
        REQUIRE_NE(plain, nullptr);
        CHECK_EQ(std::string(plain), "/repo/pkg/__wycache__/mod.wyd");
        wy_allocator_free(allocator, plain);

        char* prefixed = wy_import_cache_path(allocator, "/tmp/cache", "/repo/pkg/mod.wy");
        REQUIRE_NE(prefixed, nullptr);
        // The source's absolute directory appended to the prefix; no
        // __wycache__ component anywhere.
        CHECK_EQ(std::string(prefixed), "/tmp/cache/repo/pkg/mod.wyd");
        wy_allocator_free(allocator, prefixed);

        char* no_ext = wy_import_cache_path(allocator, nullptr, "plainname");
        REQUIRE_NE(no_ext, nullptr);
        // Relative sources are made absolute against the cwd first.
        CHECK_EQ(std::string(no_ext),
            (std::filesystem::current_path() / "__wycache__" / "plainname.wyd").string());
        wy_allocator_free(allocator, no_ext);
    }

    TEST_CASE("cache store is best-effort and validity follows the source mtime") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        wy_allocator* allocator = wy_context_get_machine(ctx)->allocator;
        REQUIRE_NE(allocator, nullptr);

        const std::string source = (tmp.root / "m.wy").string();
        write_file(source, "fn f():\n    return 1\n");
        double mtime = 0;
        REQUIRE_EQ(wy_import_cache_source_mtime(source.c_str(), &mtime), true);

        char* cache_path = wy_import_cache_path(allocator, nullptr, source.c_str());
        REQUIRE_NE(cache_path, nullptr);
        // Absent cache: never valid.
        CHECK_EQ(wy_import_cache_valid(cache_path, mtime), false);

        const wy_u8 blob[] = {1, 2, 3};
        CHECK_EQ(wy_import_cache_store(allocator, cache_path, blob, 3, nullptr), true);
        CHECK_EQ(wy_import_cache_valid(cache_path, mtime), true);

        // A source touched after the cache write invalidates it.
        double later = mtime + 10.0;
        CHECK_EQ(wy_import_cache_valid(cache_path, later), false);

        // Unwritable location: store answers false instead of failing.
        const char* reason = nullptr;
        CHECK_EQ(wy_import_cache_store(allocator, "/proc/definitely/not/writable/x.wyd",
            blob, 3, &reason), false);
        CHECK_NE(reason, nullptr);

        wy_allocator_free(allocator, cache_path);
    }

    TEST_CASE("a .wy source compiles through the cache; the builtin row bars source lookup") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        write_file(tmp.root / "user" / "mod.wy", "the user source");
        write_file(tmp.root / "user" / "mod.wyc", "stale pypoc image");

        static int compile_calls = 0;
        static std::string last_source;
        auto compile_fake = [](void*, wy_context* context, const char*, const wy_u8* source,
            wy_uword source_len, wy_u8** out_bytes, wy_uword* out_len, const char**) -> wy_error {
            compile_calls += 1;
            last_source.assign(reinterpret_cast<const char*>(source), source_len);
            *out_len = source_len;
            *out_bytes = static_cast<wy_u8*>(wy_context_gc_alloc(context, *out_len));
            if (*out_bytes == nullptr) { return WY_ERR_NOMEM; }
            std::memcpy(*out_bytes, source, *out_len);
            return WY_ERR_NONE;
        };

        static const wy_module_image dummy_image = { "dummy", {} };
        const wy_import_fs_builtin table[] = {
            { "wyrm::tools::compile_source", &dummy_image },
        };

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);
        search.compile = compile_fake;
        search.builtins = table;
        search.builtin_count = 1;

        // First resolution: compiles the .wy (not the sibling .wyc) and
        // writes the cache.
        const std::string path = "user::mod";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "the user source");
        CHECK_EQ(compile_calls, 1);
        CHECK_EQ(last_source, "the user source");
        // The cache lives beside the source's own directory.
        CHECK_EQ(std::filesystem::exists(tmp.root / "user" / "__wycache__" / "mod.wyd"), true);

        // Second resolution: served from the cache, no second compile.
        bytes = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "the user source");
        CHECK_EQ(compile_calls, 1);

        // A builtin-table module is never compiled from its source - that
        // path needs the compiler to compile itself. Even with the source
        // present, the table answers.
        std::filesystem::create_directories(tmp.root / "wyrm" / "tools");
        write_file(tmp.root / "wyrm" / "tools" / "compile_source.wy", "the compiler source");
        wy_u8* barred = WY_NULL;
        wy_uword barred_len = 0;
        const wy_module_image* barred_image = WY_NULL;
        const std::string barred_path = "wyrm::tools::compile_source";
        CHECK_EQ(wy_import_fs_hook(ctx, barred_path.data(), barred_path.size(),
            &barred, &barred_len, &barred_image, &search), WY_ERR_NONE);
        CHECK_EQ(barred, WY_NULL);
        CHECK_EQ(barred_image, &dummy_image);
        CHECK_EQ(compile_calls, 1);

        // No compile hook: .wy sources are invisible (pre-M3 behavior) -
        // the resolution falls through to the sibling precompiled .wyc.
        wy_import_fs_search_path search2;
        wy_import_fs_search_path_init_s(&search2);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search2,
            tmp.root.string().c_str()), WY_ERR_NONE);
        std::filesystem::remove_all(tmp.root / "user" / "__wycache__");
        wy_u8* none = WY_NULL;
        wy_uword none_len = 0;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &none, &none_len, &image, &search2), WY_ERR_NONE);
        REQUIRE_NE(none, WY_NULL);
        CHECK_EQ(read_out(none, none_len), "stale pypoc image");
    }

    TEST_CASE("a package's __init__ beats a module of the same name in its root") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir tmp;
        write_file(tmp.root / "pkg" / "__init__.wyc", "package");
        write_file(tmp.root / "pkg.wyc", "module");

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            tmp.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "pkg";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "package");

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("a bare directory is a namespace package; a module in a later root beats it") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();
        temp_dir first;
        temp_dir second;
        write_file(first.root / "ns" / "leaf.wyc", "leaf");

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            first.root.string().c_str()), WY_ERR_NONE);
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator, &search,
            second.root.string().c_str()), WY_ERR_NONE);

        const std::string path = "ns";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        CHECK_EQ(bytes, WY_NULL);
        CHECK_EQ(image, WY_NULL);

        write_file(second.root / "ns.wyc", "module");
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        REQUIRE_NE(bytes, WY_NULL);
        CHECK_EQ(read_out(bytes, len), "module");

        wy_import_fs_search_path_finalize_f(wy_context_get_machine(ctx)->allocator, &search);
    }

    TEST_CASE("builtin table rows filed under a path make it a namespace package") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        static const wy_module_image dummy_image = { "dummy", {} };
        const wy_import_fs_builtin table[] = {
            { "std::thing", &dummy_image },
        };

        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        search.builtins = table;
        search.builtin_count = 1;

        const std::string path = "std";
        wy_u8* bytes = WY_NULL;
        wy_uword len = 0;
        const wy_module_image* image = WY_NULL;
        CHECK_EQ(wy_import_fs_hook(ctx, path.data(), path.size(), &bytes, &len, &image, &search), WY_ERR_NONE);
        CHECK_EQ(bytes, WY_NULL);
        CHECK_EQ(image, WY_NULL);

        // "st" is a prefix of the row's spelling, not a package of it.
        const std::string partial = "st";
        CHECK_EQ(wy_import_fs_hook(ctx, partial.data(), partial.size(), &bytes, &len, &image, &search), WY_ERR_UNBOUND);
    }
}
