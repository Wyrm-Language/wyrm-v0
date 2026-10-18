#ifndef WYRM_TEST_CBOR_BUILD_H_
#define WYRM_TEST_CBOR_BUILD_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <wyrm/image.h>

// A small encoder for the wire encoding (project/design/wire_encoding.md),
// just enough to hand-build module images in tests. It always emits the
// canonical form.

namespace wy_test_cbor
{

using Bytes = std::vector<std::uint8_t>;

inline Bytes head(unsigned major, std::uint64_t n)
{
    Bytes out;
    std::uint8_t base = (std::uint8_t) (major << 5);
    int width = 0;
    if (n < 24) {
        out.push_back((std::uint8_t) (base | n));
        return out;
    } else if (n < 0x100) {
        out.push_back(base | 24); width = 1;
    } else if (n < 0x10000) {
        out.push_back(base | 25); width = 2;
    } else if (n < 0x100000000ull) {
        out.push_back(base | 26); width = 4;
    } else {
        out.push_back(base | 27); width = 8;
    }
    for (int i = width - 1; i >= 0; i--) { out.push_back((std::uint8_t) (n >> (8 * i))); }
    return out;
}

inline Bytes cat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const Bytes& p : parts) { out.insert(out.end(), p.begin(), p.end()); }
    return out;
}

inline Bytes uval(std::uint64_t n) { return head(0, n); }
inline Bytes integer(std::int64_t v) { return v >= 0 ? head(0, (std::uint64_t) v) : head(1, (std::uint64_t) (-1 - v)); }
inline Bytes text(const std::string& s)
{
    Bytes out = head(3, s.size());
    out.insert(out.end(), s.begin(), s.end());
    return out;
}
inline Bytes byte_string(const Bytes& b)
{
    Bytes out = head(2, b.size());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}
inline Bytes symbol(const std::string& s)
{
    Bytes out = head(6, 280);
    Bytes t = text(s);
    out.insert(out.end(), t.begin(), t.end());
    return out;
}
inline Bytes character(std::uint32_t cp)
{
    Bytes out = head(6, 282);
    Bytes t = uval(cp);
    out.insert(out.end(), t.begin(), t.end());
    return out;
}
inline Bytes boolean(bool v) { return Bytes{(std::uint8_t) (v ? 0xF5 : 0xF4)}; }
inline Bytes nil() { return Bytes{0xF6}; }
inline Bytes float64(double d)
{
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    Bytes out{0xFB};
    for (int i = 7; i >= 0; i--) { out.push_back((std::uint8_t) (bits >> (8 * i))); }
    return out;
}
inline Bytes array(const std::vector<Bytes>& items)
{
    Bytes out = head(4, items.size());
    for (const Bytes& i : items) { out.insert(out.end(), i.begin(), i.end()); }
    return out;
}

using Pair = std::pair<Bytes, Bytes>;

// A map from text keys; the pairs keep the order given.
inline Bytes map(const std::vector<Pair>& pairs)
{
    Bytes out = head(5, pairs.size());
    for (const Pair& p : pairs) {
        out.insert(out.end(), p.first.begin(), p.first.end());
        out.insert(out.end(), p.second.begin(), p.second.end());
    }
    return out;
}
inline Pair kv(const std::string& key, const Bytes& value) { return Pair{text(key), value}; }

// The 11-byte preamble: array(2), "WYCR", uint32 version.
inline Bytes preamble(std::uint32_t version = WY_IMAGE_VERSION)
{
    return Bytes{0x82, 0x64, 'W', 'Y', 'C', 'R', 0x1A,
        (std::uint8_t) (version >> 24), (std::uint8_t) (version >> 16), (std::uint8_t) (version >> 8), (std::uint8_t) version};
}

// The code section value: a byte string of little-endian u32 words.
inline Bytes code_item(const std::vector<std::uint32_t>& words)
{
    Bytes raw;
    for (std::uint32_t w : words) {
        for (int i = 0; i < 4; i++) { raw.push_back((std::uint8_t) (w >> (8 * i))); }
    }
    return byte_string(raw);
}

struct Section
{
    std::uint8_t id;
    Bytes value;   // a complete encoded item
};

// A whole file: the preamble, then one map from section id to value, in the
// order given.
inline Bytes container(const std::vector<Section>& sections, std::uint32_t version = WY_IMAGE_VERSION)
{
    Bytes out = preamble(version);
    Bytes h = head(5, sections.size());
    out.insert(out.end(), h.begin(), h.end());
    for (const Section& s : sections) {
        Bytes k = uval(s.id);
        out.insert(out.end(), k.begin(), k.end());
        out.insert(out.end(), s.value.begin(), s.value.end());
    }
    return out;
}

}  // namespace wy_test_cbor

#endif
