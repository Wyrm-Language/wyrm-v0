#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <wyrm/symtab.h>
#include <test_common/test_allocator_fixture.h>

TEST_SUITE("symtab") {
    TEST_CASE("intern is idempotent for identical text") {
        test_allocator_fixture alloc;
        wy_symtab tab{};
        REQUIRE_EQ(wy_symtab_init_f(&tab, alloc.ptr()), WY_ERR_NONE);

        wy_symbol a = wy_symtab_intern(&tab, "hello", 5);
        wy_symbol b = wy_symtab_intern(&tab, "hello", 5);
        REQUIRE_NE(a, WY_SYMBOL_INVALID);
        REQUIRE_EQ(a, b);
        REQUIRE_EQ(tab.count, 1u);

        wy_symtab_finalize_f(&tab);
    }

    TEST_CASE("identical 31-codepoint prefix, different tails intern to the same pointer") {
        test_allocator_fixture alloc;
        wy_symtab tab{};
        REQUIRE_EQ(wy_symtab_init_f(&tab, alloc.ptr()), WY_ERR_NONE);

        std::string prefix(31, 'x');
        std::string s1 = prefix + "aaaaaaaa";
        std::string s2 = prefix + "zzzzzzzzzzzzzzzz";

        wy_symbol a = wy_symtab_intern(&tab, s1.c_str(), s1.size());
        wy_symbol b = wy_symtab_intern(&tab, s2.c_str(), s2.size());

        REQUIRE_NE(a, WY_SYMBOL_INVALID);
        REQUIRE_EQ(a, b);
        REQUIRE_EQ(tab.count, 1u);
        // Full text of the first interned string wins; it must be usable as
        // a plain, correctly NUL-terminated C string.
        CHECK_EQ(std::string(a), s1);

        wy_symtab_finalize_f(&tab);
    }

    TEST_CASE("difference within the first 31 codepoints interns to different pointers") {
        test_allocator_fixture alloc;
        wy_symtab tab{};
        REQUIRE_EQ(wy_symtab_init_f(&tab, alloc.ptr()), WY_ERR_NONE);

        std::string s1 = std::string(30, 'x') + "a" + "tail_one";
        std::string s2 = std::string(30, 'x') + "b" + "tail_two";

        wy_symbol a = wy_symtab_intern(&tab, s1.c_str(), s1.size());
        wy_symbol b = wy_symtab_intern(&tab, s2.c_str(), s2.size());

        REQUIRE_NE(a, WY_SYMBOL_INVALID);
        REQUIRE_NE(b, WY_SYMBOL_INVALID);
        CHECK_NE(a, b);
        REQUIRE_EQ(tab.count, 2u);

        wy_symtab_finalize_f(&tab);
    }

    TEST_CASE("growth past ~1024 distinct symbols still finds every one correctly") {
        test_allocator_fixture alloc;
        wy_symtab tab{};
        REQUIRE_EQ(wy_symtab_init_f(&tab, alloc.ptr()), WY_ERR_NONE);

        constexpr int n = 2000;
        std::vector<std::string> names;
        std::vector<wy_symbol> symbols;
        names.reserve(n);
        symbols.reserve(n);

        for (int i = 0; i < n; ++i) {
            names.push_back("symbol_" + std::to_string(i));
        }

        for (auto& name : names) {
            wy_symbol s = wy_symtab_intern(&tab, name.c_str(), name.size());
            REQUIRE_NE(s, WY_SYMBOL_INVALID);
            symbols.push_back(s);
        }

        REQUIRE_EQ(tab.count, (wy_uword) n);

        for (int i = 0; i < n; ++i) {
            wy_symbol s = wy_symtab_intern(&tab, names[i].c_str(), names[i].size());
            CHECK_EQ(s, symbols[i]);
        }

        wy_symtab_finalize_f(&tab);
    }

    TEST_CASE("multi-byte UTF-8 near the 31-codepoint boundary does not split a codepoint") {
        test_allocator_fixture alloc;
        wy_symtab tab{};
        REQUIRE_EQ(wy_symtab_init_f(&tab, alloc.ptr()), WY_ERR_NONE);

        // 30 ASCII codepoints followed by a 3-byte UTF-8 codepoint (e.g. U+2603
        // SNOWMAN, encoded as E2 98 83) straddling the 31st-codepoint boundary,
        // then differing tails.
        std::string head(30, 'x');
        std::string snowman = "\xE2\x98\x83";

        std::string s1 = head + snowman + "tail_aaaa";
        std::string s2 = head + snowman + "tail_bbbbbbbb";

        // Significant prefix should be head (30 codepoints) + snowman (31st
        // codepoint) = 30 + 3 = 33 bytes, never splitting the snowman's
        // continuation bytes.
        wy_uword sig_len = wy_symtab_significant_prefix_len(s1.c_str(), s1.size());
        CHECK_EQ(sig_len, 33u);

        wy_symbol a = wy_symtab_intern(&tab, s1.c_str(), s1.size());
        wy_symbol b = wy_symtab_intern(&tab, s2.c_str(), s2.size());

        REQUIRE_NE(a, WY_SYMBOL_INVALID);
        REQUIRE_EQ(a, b);
        CHECK_EQ(std::string(a), s1);

        // A tail that changes a byte inside the snowman's own encoding (i.e.
        // before byte offset 33) must differ.
        std::string s3 = head + "\xE2\x98\x84" + "tail_aaaa";
        wy_symbol c = wy_symtab_intern(&tab, s3.c_str(), s3.size());
        CHECK_NE(c, a);

        wy_symtab_finalize_f(&tab);
    }
}
