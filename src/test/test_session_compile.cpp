#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <std/io_native.h>
#include "../embed/builtins.h"

#include <wyrm.h>
#include <wyrm/builtins.h>
#include <wyrm/bytes.h>
#include <wyrm/error.h>
#include <wyrm/fiber.h>
#include <wyrm/image_loader.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/module.h>
#include <wyrm/platform/hosted/expand_native.h>
#include <wyrm/session.h>
#include <wyrm/string.h>
#include <wyrm/vm.h>

#include <test_common/test_context_fixture.h>

// REPL session, milestone M2 (doc-llm/history/repl/repl-plan.md): the compiler side. The same
// context that hosts the session also runs the embedded compiler (as the real
// driver will), so a session's compile-time state and its loaded module share
// one process. Every input goes compile -> delta container -> wy_module_extend
// -> wy_module_run_function, and output is captured through the io hook.

namespace {

void capture_(wy_context*, const char* bytes, wy_uword len, void* ud)
{
    static_cast<std::string*>(ud)->append(bytes, len);
}

// Library imports come from the compiled-in table; anything else from `root`
// (.wyd fixtures), the way the golden harness does it.
wy_error import_(wy_context* ctx, const char* path, wy_uword len, wy_u8** out, wy_uword* out_len,
    const wy_module_image** out_image, void* ud)
{
    std::string relative(path, len);
    for (std::size_t pos = 0; (pos = relative.find("::", pos)) != std::string::npos;) { relative.replace(pos, 2, "/"); }
    // design/modules.md M1, as the hosted hook does it: a package's
    // __init__ beats a module, and a bare directory is a namespace package.
    std::string base = *static_cast<std::string*>(ud) + "/" + relative;
    for (const std::string& file : {base + "/__init__.wyd", base + ".wyd"}) {
        std::ifstream f(file, std::ios::binary);
        if (!f.good()) { continue; }
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string bytes = ss.str();
        *out = static_cast<wy_u8*>(wy_context_gc_alloc(ctx, bytes.size()));
        if (*out == nullptr) { return WY_ERR_NOMEM; }
        std::memcpy(*out, bytes.data(), bytes.size());
        *out_len = bytes.size();
        return WY_ERR_NONE;
    }
    bool children = false;
    for (wy_uword i = 0; i < wyrm_builtin_module_count; i++) {
        const char* row = wyrm_builtin_modules[i].path;
        if (std::strlen(row) == len && std::memcmp(row, path, len) == 0) {
            *out_image = wyrm_builtin_modules[i].image;
            return WY_ERR_NONE;
        }
        children = children || (std::strlen(row) > len + 2 && std::memcmp(row, path, len) == 0 && row[len] == ':' && row[len + 1] == ':');
    }
    // A namespace package: a directory, or rows under it with no row of its own.
    return children || std::filesystem::is_directory(base) ? WY_ERR_NONE : WY_ERR_UNBOUND;
}

struct harness
{
    test_context_fixture fix;
    wy_context* ctx = nullptr;
    std::string out;
    std::string import_root;
    wy_module* host = nullptr;      // the session module (what runs)
    wy_module* compiler = nullptr;  // wyrm::tools::compile_source
    wy_value session = {};          // the compile-side SessionContext (rooted)
    wy_value pieces = {};           // scratch list, rooted
    wy_value fn_new = {}, fn_compile = {}, fn_tree = {}, fn_pieces = {}, fn_undo = {};
    std::string last_error;

    explicit harness(const std::string& root = std::string(WY_TEST_FIXTURE_DIR), wy_uword gc_threshold = WY_CONTEXT_GC_THRESHOLD_DEFAULT,
        wy_uword code_words = 0)
        : import_root(root)
    {
        ctx = fix.get_context_ptr();
        ctx->gc_threshold = gc_threshold;
        if (gc_threshold != WY_CONTEXT_GC_THRESHOLD_DEFAULT) { ctx->gc_growth_factor = 0; }
        wy_fiber* fiber = wy_fiber_create(ctx, 65536, 4096);
        REQUIRE_NE(fiber, WY_NULL);
        REQUIRE_EQ(wy_context_attach_fiber(ctx, fiber), WY_ERR_NONE);
        ctx->io.write = capture_;
        ctx->io.ud = &out;
        ctx->import_hook = import_;
        ctx->import_ud = &import_root;

        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(ctx, &builtins), WY_ERR_NONE);
        ctx->builtins = builtins;
        REQUIRE_EQ(wy_io_natives_install(ctx), WY_ERR_NONE);
        REQUIRE_EQ(wy_expand_module_install(ctx), WY_ERR_NONE);

        wy_session_config config;
        wy_session_config_default(&config);
        if (code_words != 0) { config.capacity[WY_SESSION_CODE] = code_words; }
        REQUIRE_EQ(wy_module_session_new(ctx, &config, &host), WY_ERR_NONE);

        wy_string* path = WY_NULL;
        REQUIRE_EQ(wy_string_strdup(ctx, "wyrm::tools::compile_source", &path), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_import(ctx, path, &compiler), WY_ERR_NONE);
        REQUIRE_EQ(wy_module_run_init(ctx, compiler), WY_ERR_NONE);
        fn_new = export_of("session_new");
        fn_compile = export_of("session_compile");
        fn_tree = export_of("session_compile_tree");
        fn_pieces = export_of("session_pieces");
        fn_undo = export_of("session_undo");
        for (wy_value fn : {fn_new, fn_compile, fn_tree, fn_pieces, fn_undo}) {
            REQUIRE_EQ(fn.type, WY_TYPE_TAG_FUNCTION);
        }

        REQUIRE_EQ(wy_context_root_push_f(ctx, &session), WY_ERR_NONE);
        REQUIRE_EQ(wy_context_root_push_f(ctx, &pieces), WY_ERR_NONE);
        session = call(fn_new, {});
    }

    wy_value export_of(const char* name)
    {
        wy_symbol sym = WY_NULL;
        REQUIRE_EQ(wy_context_intern(ctx, name, std::strlen(name), &sym), WY_ERR_NONE);
        wy_uword slot = wy_slot_dict_get(&compiler->exports, sym);
        REQUIRE_NE(slot, WY_SLOT_INVALID);
        return compiler->globals[slot];
    }

    wy_value call(wy_value fn, std::vector<wy_value> args)
    {
        wy_value result = wy_value_nil();
        wy_error err = wy_vm_call_sync(ctx, fn, args.data(), args.size(), &result, 1);
        REQUIRE_EQ(err, WY_ERR_NONE);
        return result;
    }

    wy_value string_value(const std::string& text)
    {
        wy_string* s = WY_NULL;
        REQUIRE_EQ(wy_string_new(ctx, text.data(), text.size(), &s), WY_ERR_NONE);
        return wy_value_object(WY_TYPE_TAG_STR, (wy_object*) s);
    }

    static std::string error_text(wy_value v)
    {
        auto* e = (wy_error_obj*) v.data.gc_object;
        return e->what ? std::string(e->what->str, e->what->len) : std::string("(error)");
    }

    // Feed a compile result (delta bytes, or an error value) to the session.
    // Answers WY_ERR_NONE and sets `value`; else records the failure in last_error.
    wy_error apply(wy_value blob, wy_value* value = nullptr)
    {
        last_error.clear();
        if (blob.type == WY_TYPE_TAG_ERROR) { last_error = error_text(blob); return WY_ERR_IMAGE; }
        REQUIRE_EQ(blob.type, WY_TYPE_TAG_BYTES);
        auto* b = (wy_bytes*) blob.data.gc_object;
        wy_module_image image;
        wy_error err = wy_image_from_bytes(b->data, b->len, &image);
        if (err != WY_ERR_NONE) { last_error = "container"; return err; }
        wy_uword init = 0;
        err = wy_module_extend(ctx, host, &image, &init);
        if (err != WY_ERR_NONE) { last_error = "extend"; return err; }
        wy_value result = wy_value_nil();
        err = wy_module_run_function(ctx, host, init, &result);
        if (err != WY_ERR_NONE) { last_error = "run"; return err; }
        if (value != nullptr) { *value = result; }
        return WY_ERR_NONE;
    }

    wy_error input(const std::string& src, wy_value* value = nullptr)
    {
        return apply(call(fn_compile, {session, string_value(src)}), value);
    }

    // Run `src` as one input per top-level statement (exact boundaries).
    wy_error run_pieces(const std::string& src)
    {
        pieces = call(fn_pieces, {string_value(src)});
        if (pieces.type == WY_TYPE_TAG_ERROR) { last_error = error_text(pieces); return WY_ERR_IMAGE; }
        REQUIRE_EQ(pieces.type, WY_TYPE_TAG_LIST);
        auto* list = (wy_list*) pieces.data.gc_object;
        for (wy_uword i = 0; i < list->count; i++) {
            wy_error err = apply(call(fn_tree, {session, list->items[i]}));
            if (err != WY_ERR_NONE) { return err; }
            list = (wy_list*) pieces.data.gc_object;  // (a collection may not move it, but re-read anyway)
        }
        return WY_ERR_NONE;
    }
};

void run_differential(wy_uword gc_threshold, const std::vector<std::string>& only = {})
{
    // The differential: the manifest's `.out` is the truth for the whole-file run,
    // so it is also the truth for the piece-by-piece run.
    std::ifstream manifest(std::string(WY_TEST_CORPUS_DIR) + "/manifest.txt");
    REQUIRE(manifest.good());
    // Fixtures that need a host seam a bare session does not provide (__ARGS,
    // files written into the working directory, the expansion harness).
    const std::vector<std::string> skip = {"samples/eval_args.wy", "io_file.wy", "bytes_header/cvm_header.wy"};
    std::string line;
    int ran = 0;
    std::vector<std::string> failures;
    while (std::getline(manifest, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        std::istringstream row(line);
        std::string source, out, status;
        std::getline(row, source, '\t');
        std::getline(row, out, '\t');
        std::getline(row, status, '\t');
        if ((status != "matches" && status != "local-only") || out == "-") { continue; }
        if (!only.empty()) {
            bool wanted = false;
            for (const auto& o : only) { wanted = wanted || o == source; }
            if (!wanted) { continue; }
        }
        if (source.rfind("expand/", 0) == 0) { continue; }
        bool skipped = false;
        for (const auto& k : skip) { skipped = skipped || k == source; }
        if (skipped) { continue; }

        std::ifstream src_file(std::string(WY_TEST_CORPUS_DIR) + "/" + source);
        std::ifstream out_file(std::string(WY_TEST_CORPUS_DIR) + "/" + out);
        REQUIRE(src_file.good());
        REQUIRE(out_file.good());
        std::stringstream src_text, out_text;
        src_text << src_file.rdbuf();
        out_text << out_file.rdbuf();

        std::string dir = source.find('/') == std::string::npos ? "" : "/" + source.substr(0, source.find_last_of('/'));
        harness h(std::string(WY_TEST_FIXTURE_DIR) + dir, gc_threshold);
        wy_error err = h.run_pieces(src_text.str());
        std::string why;
        if (err != WY_ERR_NONE) { why = "failed at " + h.last_error; }
        else if (h.out != out_text.str()) { why = "output differs"; }
        if (!why.empty()) { failures.push_back(source + ": " + why); }
        ran++;
    }
    for (const auto& f : failures) { MESSAGE(f); }
    CHECK_GT(ran, only.empty() ? 15 : 0);
    CHECK(failures.empty());
}

}  // namespace

TEST_SUITE("session-compile")
{
    TEST_CASE("an input's value comes back, and state carries to the next input")
    {
        harness h;
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("x := 40", &v), WY_ERR_NONE);
        REQUIRE_EQ(h.input("x + 2", &v), WY_ERR_NONE);
        REQUIRE_EQ(v.type, WY_TYPE_TAG_WORD);
        CHECK_EQ(v.data.word, 42);
        REQUIRE_EQ(h.input("println(x)"), WY_ERR_NONE);
        CHECK_EQ(h.out, "40\n");
    }

    TEST_CASE("functions defined in one input are callable from the next")
    {
        harness h;
        REQUIRE_EQ(h.input("fn twice(n):\n    return n * 2\n"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("twice(21)", &v), WY_ERR_NONE);
        REQUIRE_EQ(v.type, WY_TYPE_TAG_WORD);
        CHECK_EQ(v.data.word, 42);
    }

    TEST_CASE("redeclaring shadows: earlier callers keep the old binding")
    {
        harness h;
        REQUIRE_EQ(h.input("fn a():\n    return 1\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("fn b():\n    return a()\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("fn a():\n    return 2\n"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("b()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 1);           // b was compiled against the first a
        REQUIRE_EQ(h.input("a()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 2);           // new inputs see the newest a
    }

    TEST_CASE("assignment rebinds the existing slot, so callers see it")
    {
        harness h;
        REQUIRE_EQ(h.input("fn a():\n    return 1\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("fn b():\n    return a()\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("a = fn(): 3"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("b()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 3);
    }

    TEST_CASE("a forward reference is filled by the first later definition")
    {
        harness h;
        REQUIRE_EQ(h.input("fn b():\n    return a()\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("fn a():\n    return 7\n"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("b()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 7);
        // ... and a later redeclaration shadows rather than refilling.
        REQUIRE_EQ(h.input("fn a():\n    return 8\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("b()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 7);
        REQUIRE_EQ(h.input("a()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 8);
    }

    TEST_CASE("calling a name nothing defined yet faults at run time, not at compile time")
    {
        harness h;
        CHECK_NE(h.input("not_yet()"), WY_ERR_NONE);
        CHECK_EQ(h.last_error, "run");
        REQUIRE_EQ(h.input("fn not_yet():\n    return 5\n"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("not_yet()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 5);
    }

    TEST_CASE("a failed compile leaves the session as it was")
    {
        harness h;
        REQUIRE_EQ(h.input("keep := 1"), WY_ERR_NONE);
        wy_uword code = h.host->code_len, fns = h.host->function_count, globals = h.host->global_count;
        wy_uword statics = h.host->static_count;

        CHECK_NE(h.input("zzz = 1"), WY_ERR_NONE);   // assigning to an undeclared name
        CHECK_NE(h.input("fn broken(:"), WY_ERR_NONE);  // does not even parse
        CHECK_EQ(h.host->code_len, code);
        CHECK_EQ(h.host->function_count, fns);
        CHECK_EQ(h.host->global_count, globals);
        CHECK_EQ(h.host->static_count, statics);

        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("keep + 1", &v), WY_ERR_NONE);   // and it still works, numbering intact
        CHECK_EQ(v.data.word, 2);
    }

    TEST_CASE("a bad input in the middle does not disturb what came before")
    {
        harness h;
        REQUIRE_EQ(h.input("fn f():\n    return 10\n"), WY_ERR_NONE);
        CHECK_NE(h.input("fn g():\n    return f() +\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("fn g():\n    return f() + 1\n"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("g()", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 11);
    }

    TEST_CASE("classes and methods work across inputs (one message table)")
    {
        harness h;
        REQUIRE_EQ(h.input("class Box:\n    slot v: int = 3\n\n    fn get() -> int:\n        return this.v\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("b := Box()"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("b!get()", &v), WY_ERR_NONE);
        REQUIRE_EQ(v.type, WY_TYPE_TAG_WORD);
        CHECK_EQ(v.data.word, 3);
    }

    TEST_CASE("imports work in an input")
    {
        harness h;
        REQUIRE_EQ(h.input("import std::io"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("std::io::println(\"hi\")"), WY_ERR_NONE);
        CHECK_EQ(h.out, "hi\n");
    }

    TEST_CASE("a refused extend (session full) is undone on the compile side, so later inputs still load")
    {
        harness h(std::string(WY_TEST_FIXTURE_DIR), WY_CONTEXT_GC_THRESHOLD_DEFAULT, 150);
        REQUIRE_EQ(h.input("keep := 1"), WY_ERR_NONE);

        std::string big = "fn big():\n";
        for (int i = 0; i < 100; i++) { big += "    println(1)\n"; }
        wy_uword code = h.host->code_len, fns = h.host->function_count, globals = h.host->global_count;
        CHECK_EQ(h.input(big), WY_ERR_SESSION_FULL);   // the loader refuses it: nothing fits
        CHECK_EQ(h.last_error, "extend");
        CHECK_EQ(h.host->code_len, code);
        CHECK_EQ(h.host->function_count, fns);
        CHECK_EQ(h.host->global_count, globals);

        // Without the undo the compiler would be one input ahead and every
        // later delta would be refused as out of step.
        h.call(h.fn_undo, {h.session});
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("keep + 1", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 2);
    }

    TEST_CASE("a coroutine suspended before an extend resumes after it")
    {
        harness h;
        REQUIRE_EQ(h.input("co gen():\n    yield 1\n    yield 2\n"), WY_ERR_NONE);
        REQUIRE_EQ(h.input("g := gen()"), WY_ERR_NONE);
        wy_value v = wy_value_nil();
        REQUIRE_EQ(h.input("next(g)", &v), WY_ERR_NONE);
        CHECK_EQ(v.data.word, 1);
        // Grow the module (code, functions, statics) while g is suspended inside it.
        for (int i = 0; i < 8; i++) {
            REQUIRE_EQ(h.input("fn pad" + std::to_string(i) + "():\n    return \"pad\"\n"), WY_ERR_NONE);
        }
        REQUIRE_EQ(h.input("next(g)", &v), WY_ERR_NONE);
        REQUIRE_EQ(v.type, WY_TYPE_TAG_WORD);
        CHECK_EQ(v.data.word, 2);
    }
}

TEST_SUITE("session-differential")
{
    TEST_CASE("every runnable corpus source, run one statement per input, prints what the whole file prints")
    {
        run_differential(WY_CONTEXT_GC_THRESHOLD_DEFAULT);
    }

    TEST_CASE("a subset of it with a collection at every safepoint")
    {
        // Every safepoint collects while the whole compiler runs in this VM, so
        // only the small, varied fixtures.
        run_differential(0, {"hello.wy", "closures.wy", "classes.wy", "coroutines.wy"});
    }
}
