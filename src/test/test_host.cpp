#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <wyrm/host.h>

// libwyrmhost's public facade (include/wyrm/host.h), used exactly as an embedder
// would: only wy_host_* calls (plus wy_value to hold a result).

namespace {

struct host_ptr
{
    wy_host* host = nullptr;
    std::string out;
    explicit host_ptr(const std::string& include = std::string())
    {
        wy_host_config config;
        wy_host_config_default(&config);
        config.output = capture;
        config.output_ud = this;
        const char* roots[1] = {include.c_str()};
        if (!include.empty()) { config.include_paths = roots; config.include_count = 1; }
        REQUIRE_EQ(wy_host_new(&config, &host), WY_ERR_NONE);
        REQUIRE_NE(host, nullptr);
    }
    ~host_ptr() { wy_host_free(host); }
    static void capture(wy_context*, const char* bytes, wy_uword len, void* ud)
    {
        static_cast<host_ptr*>(ud)->out.append(bytes, len);
    }
    operator wy_host*() const { return host; }
};

std::filesystem::path temp_dir()
{
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("wyrm_host_test_" + std::to_string(std::random_device{}()) + "_" + std::to_string(counter++));
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_SUITE("host")
{
    TEST_CASE("a host can be created and destroyed repeatedly")
    {
        for (int i = 0; i < 3; i++) {
            host_ptr h;
            wy_value v;
            REQUIRE_EQ(wy_host_eval(h, "1 + 1", &v), WY_ERR_NONE);
            CHECK_EQ(v.data.word, 2);
        }
    }

    TEST_CASE("eval answers a trailing expression and keeps state between calls")
    {
        host_ptr h;
        wy_value v;
        REQUIRE_EQ(wy_host_eval(h, "x := 40", nullptr), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_eval(h, "x + 2", &v), WY_ERR_NONE);
        REQUIRE_EQ(v.type, WY_TYPE_TAG_WORD);
        CHECK_EQ(v.data.word, 42);
        REQUIRE_EQ(wy_host_eval(h, "println(x)", nullptr), WY_ERR_NONE);
        CHECK_EQ(h.out, "40\n");
    }

    TEST_CASE("variables set from C are visible to code, and the other way round")
    {
        host_ptr h;
        REQUIRE_EQ(wy_host_set_int(h, "width", 40), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_set_float(h, "ratio", 0.5), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_set_bool(h, "on", true), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_set_string(h, "title", "hello"), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_set_nil(h, "nothing"), WY_ERR_NONE);

        REQUIRE_EQ(wy_host_eval(h, "area := width * 2\nlabel := title + \" world\"", nullptr), WY_ERR_NONE);

        long area = 0;
        REQUIRE_EQ(wy_host_get_int(h, "area", &area), WY_ERR_NONE);
        CHECK_EQ(area, 80);
        char text[64];
        size_t len = 0;
        REQUIRE_EQ(wy_host_get_string(h, "label", text, sizeof(text), &len), WY_ERR_NONE);
        CHECK_EQ(std::string(text), "hello world");
        CHECK_EQ(len, 11u);
        double ratio = 0;
        REQUIRE_EQ(wy_host_get_float(h, "ratio", &ratio), WY_ERR_NONE);
        CHECK_EQ(ratio, 0.5);
        double as_float = 0;
        REQUIRE_EQ(wy_host_get_float(h, "width", &as_float), WY_ERR_NONE);   // an int converts
        CHECK_EQ(as_float, 40.0);
        bool on = false;
        REQUIRE_EQ(wy_host_get_bool(h, "on", &on), WY_ERR_NONE);
        CHECK(on);
        CHECK(wy_host_has(h, "area"));
        CHECK_FALSE(wy_host_has(h, "never_defined"));
    }

    TEST_CASE("getters report a missing name, a wrong type and a bad name")
    {
        host_ptr h;
        REQUIRE_EQ(wy_host_set_string(h, "s", "text"), WY_ERR_NONE);
        long n = 0;
        CHECK_EQ(wy_host_get_int(h, "missing", &n), WY_ERR_UNBOUND);
        CHECK_NE(std::string(wy_host_error(h)).find("missing"), std::string::npos);
        CHECK_EQ(wy_host_get_int(h, "s", &n), WY_ERR_BAD_TYPE);
        CHECK_EQ(wy_host_get_int(h, "not a name", &n), WY_ERR_INVAL);
        CHECK_EQ(wy_host_set_int(h, "1abc", 1), WY_ERR_INVAL);
        CHECK_EQ(wy_host_set_int(h, "a;b", 1), WY_ERR_INVAL);   // no way to smuggle source in
        char tiny[3];
        size_t len = 0;
        CHECK_EQ(wy_host_get_string(h, "s", tiny, sizeof(tiny), &len), WY_ERR_RANGE);   // truncated
        CHECK_EQ(len, 4u);
        CHECK_EQ(std::string(tiny), "te");
    }

    TEST_CASE("setting an existing variable rebinds it for code that already refers to it")
    {
        host_ptr h;
        REQUIRE_EQ(wy_host_eval(h, "level := 1\nfn current():\n    return level\n", nullptr), WY_ERR_NONE);
        wy_value v;
        REQUIRE_EQ(wy_host_eval(h, "current()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 1);
        REQUIRE_EQ(wy_host_set_int(h, "level", 7), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_eval(h, "current()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 7);
    }

    TEST_CASE("a string kept in a variable survives many evaluations")
    {
        host_ptr h;
        REQUIRE_EQ(wy_host_set_string(h, "keep", "still here"), WY_ERR_NONE);
        for (int i = 0; i < 40; i++) {
            REQUIRE_EQ(wy_host_eval(h, "junk := [1, 2, 3, 4, 5, 6, 7, 8]", nullptr), WY_ERR_NONE);
        }
        char text[32];
        REQUIRE_EQ(wy_host_get_string(h, "keep", text, sizeof(text), nullptr), WY_ERR_NONE);
        CHECK_EQ(std::string(text), "still here");
    }

    TEST_CASE("errors carry a code and a message and leave the host usable")
    {
        host_ptr h;
        REQUIRE_EQ(wy_host_eval(h, "ok := 1", nullptr), WY_ERR_NONE);

        CHECK_EQ(wy_host_eval(h, "x := := 1", nullptr), WY_ERR_IMAGE);          // does not compile
        CHECK_NE(std::string(wy_host_error(h)), "");
        CHECK_EQ(wy_host_eval(h, "undeclared = 5", nullptr), WY_ERR_IMAGE);
        CHECK_NE(std::string(wy_host_error(h)).find("undeclared"), std::string::npos);
        CHECK_EQ(wy_host_eval(h, "trap(7)", nullptr), WY_ERR_FAULT);            // faults when run
        CHECK_NE(std::string(wy_host_error(h)).find("trap 7"), std::string::npos);

        wy_value v;
        REQUIRE_EQ(wy_host_eval(h, "ok + 1", &v), WY_ERR_NONE);                 // and carries on
        CHECK_EQ(v.data.word, 2);
        CHECK_EQ(std::string(wy_host_error(h)), "");
    }

    TEST_CASE("load_file runs a script in the session, with its directory as an import root")
    {
        auto dir = temp_dir();
        std::ofstream(dir / "helper.wy") << "fn double(n):\n    return n * 2\n";
        std::ofstream(dir / "main.wy") << "import helper\nanswer := helper::double(21)\nfn describe():\n    return \"answer is \" + str(answer)\n";

        host_ptr h;
        REQUIRE_EQ(wy_host_load_file(h, (dir / "main.wy").string().c_str()), WY_ERR_NONE);
        long answer = 0;
        REQUIRE_EQ(wy_host_get_int(h, "answer", &answer), WY_ERR_NONE);
        CHECK_EQ(answer, 42);
        wy_value v;
        REQUIRE_EQ(wy_host_eval(h, "describe()", &v), WY_ERR_NONE);
        char text[64];
        REQUIRE_EQ(wy_host_format(h, v, text, sizeof(text), nullptr), WY_ERR_NONE);
        CHECK_EQ(std::string(text), "answer is 42");
        std::filesystem::remove_all(dir);
    }

    TEST_CASE("load_file reports a missing file and a broken script by path")
    {
        auto dir = temp_dir();
        std::ofstream(dir / "bad.wy") << "x := := 1\n";
        host_ptr h;
        CHECK_EQ(wy_host_load_file(h, (dir / "nope.wy").string().c_str()), WY_ERR_UNBOUND);
        CHECK_NE(std::string(wy_host_error(h)).find("nope.wy"), std::string::npos);
        CHECK_EQ(wy_host_load_file(h, (dir / "bad.wy").string().c_str()), WY_ERR_IMAGE);
        CHECK_NE(std::string(wy_host_error(h)).find("bad.wy"), std::string::npos);
        std::filesystem::remove_all(dir);
    }

    TEST_CASE("script arguments can be passed as variables before loading")
    {
        auto dir = temp_dir();
        std::ofstream(dir / "greet.wy") << "message := \"hello, \" + who\n";
        host_ptr h;
        REQUIRE_EQ(wy_host_set_string(h, "who", "embedder"), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_load_file(h, (dir / "greet.wy").string().c_str()), WY_ERR_NONE);
        char text[64];
        REQUIRE_EQ(wy_host_get_string(h, "message", text, sizeof(text), nullptr), WY_ERR_NONE);
        CHECK_EQ(std::string(text), "hello, embedder");
        std::filesystem::remove_all(dir);
    }

    TEST_CASE("needs_more recognises unfinished input")
    {
        host_ptr h;
        CHECK_FALSE(wy_host_needs_more(h, "x := 1"));
        CHECK(wy_host_needs_more(h, "x := (1,"));
        CHECK(wy_host_needs_more(h, "fn f():"));
        CHECK(wy_host_needs_more(h, "fn f():\n    return 1"));
        CHECK_FALSE(wy_host_needs_more(h, "println(1)"));
    }

    TEST_CASE("format renders values the way println does")
    {
        host_ptr h;
        wy_value v;
        char text[64];
        size_t len = 0;
        REQUIRE_EQ(wy_host_eval(h, "[1, 2, 3]", &v), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_format(h, v, text, sizeof(text), &len), WY_ERR_NONE);
        CHECK_EQ(std::string(text), "[1, 2, 3]");
        CHECK_EQ(len, 9u);
        REQUIRE_EQ(wy_host_eval(h, "\"plain\"", &v), WY_ERR_NONE);
        REQUIRE_EQ(wy_host_format(h, v, text, sizeof(text), nullptr), WY_ERR_NONE);
        CHECK_EQ(std::string(text), "plain");                       // a string is its own text
        char tiny[4];
        CHECK_EQ(wy_host_format(h, v, tiny, sizeof(tiny), &len), WY_ERR_RANGE);
        CHECK_EQ(len, 5u);
        CHECK_EQ(std::string(tiny), "pla");
        CHECK(h.out.empty());                                       // formatting is not output
    }

    TEST_CASE("print output goes to the configured sink")
    {
        host_ptr h;
        REQUIRE_EQ(wy_host_eval(h, "println(\"one\")\nprint(\"two\")", nullptr), WY_ERR_NONE);
        CHECK_EQ(h.out, "one\ntwo");
    }

    TEST_CASE("wy_host_repl runs a transcript from any FILE")
    {
        host_ptr h;
        const char* transcript = "x := 20\nx + 1\nprintln(\"hi\")\nfn f(n):\n    return n * 3\n\nf(x)\n:quit\nnot reached\n";
        FILE* in = fmemopen(const_cast<char*>(transcript), std::strlen(transcript), "r");
        REQUIRE_NE(in, nullptr);
        CHECK_EQ(wy_host_repl(h, in, false), 0);
        fclose(in);
        CHECK_EQ(h.out, "21\nhi\n60\n");
        long x = 0;
        REQUIRE_EQ(wy_host_get_int(h, "x", &x), WY_ERR_NONE);      // the session outlives the loop
        CHECK_EQ(x, 20);
    }
}
