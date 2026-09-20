#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

// vm_plan/README.md: "opcode.h and image.h are copied verbatim from
// pypoc/wypoc/compiler_bc/include/wyrm/ and drift is a test failure."
// WY_TEST_DEST_INCLUDE_DIR / WY_TEST_PYPOC_INCLUDE_DIR come from
// src/test/meson.build.

namespace
{

std::string read_file(const std::string& path, bool& ok)
{
    std::ifstream f(path, std::ios::binary);
    ok = f.good();
    if (!ok) { return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// scripts/sync_pypoc_headers.py prepends a provenance comment before the
// verbatim pypoc content; strip it before comparing bodies.
std::string strip_provenance(const std::string& text)
{
    static const std::string prefix = "/* Synced verbatim";
    static const std::string marker = "*/\n";
    if (text.rfind(prefix, 0) != 0) { return text; }
    auto pos = text.find(marker);
    return pos == std::string::npos ? text : text.substr(pos + marker.size());
}

void check_header_synced(const char* name)
{
    bool pypoc_ok = false;
    std::string pypoc_src = read_file(std::string(WY_TEST_PYPOC_INCLUDE_DIR) + "/" + name, pypoc_ok);
    if (!pypoc_ok) {
        MESSAGE("pypoc/ checkout not present; skipping header sync check for ", name);
        return;
    }

    bool dest_ok = false;
    std::string dest = read_file(std::string(WY_TEST_DEST_INCLUDE_DIR) + "/" + name, dest_ok);
    REQUIRE(dest_ok);

    CHECK_EQ(strip_provenance(dest), pypoc_src);
}

}  // namespace

TEST_SUITE("headers_sync")
{
    TEST_CASE("opcode.h matches pypoc's copy verbatim")
    {
        check_header_synced("opcode.h");
    }

    TEST_CASE("image.h matches pypoc's copy verbatim")
    {
        check_header_synced("image.h");
    }
}
