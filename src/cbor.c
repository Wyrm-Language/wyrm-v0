#include <wyrm/cbor.h>

#define WY_CBOR_TAG_SYMBOL 280u
#define WY_CBOR_TAG_CHAR 282u

static wy_uword remaining_(const wy_cbor_reader* reader)
{
    return reader->len - reader->pos;
}

/**
 * Read the argument of a major 0-6 header: arg < 24 is the value itself,
 * 24-27 name a 1, 2, 4 or 8 byte big-endian follower, and 28-31 are not
 * used. A follower that a shorter form could hold is rejected, so every value
 * has one encoding.
 */
static wy_error read_arg_(wy_cbor_reader* reader, wy_u8 arg, wy_u64* out)
{
    if (arg < 24u) {
        *out = arg;
        return WY_ERR_NONE;
    }
    wy_uword width;
    wy_u64 floor;
    switch (arg) {
    case 24: width = 1; floor = 24; break;
    case 25: width = 2; floor = 0x100u; break;
    case 26: width = 4; floor = 0x10000u; break;
    case 27: width = 8; floor = 0x100000000u; break;
    default: return WY_ERR_IMAGE;
    }
    if (remaining_(reader) < width) { return WY_ERR_IMAGE; }
    wy_u64 value = 0;
    for (wy_uword i = 0; i < width; i++) {
        value = (value << 8) | reader->data[reader->pos + i];
    }
    if (value < floor) { return WY_ERR_IMAGE; }
    reader->pos += width;
    *out = value;
    return WY_ERR_NONE;
}

/** RFC 3629 UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF. */
static bool utf8_valid_(const wy_u8* text, wy_u64 len)
{
    wy_u64 i = 0;
    while (i < len) {
        wy_u8 c = text[i];
        wy_u8 lo = 0x80;
        wy_u8 hi = 0xBF;
        wy_u64 extra;
        if (c < 0x80) {
            i++;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            if (c == 0xE0) { lo = 0xA0; }
            if (c == 0xED) { hi = 0x9F; }
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            if (c == 0xF0) { lo = 0x90; }
            if (c == 0xF4) { hi = 0x8F; }
        } else {
            return false;
        }
        if (len - i <= extra) { return false; }
        if (text[i + 1] < lo || text[i + 1] > hi) { return false; }
        for (wy_u64 j = 2; j <= extra; j++) {
            if ((text[i + j] & 0xC0) != 0x80) { return false; }
        }
        i += extra + 1;
    }
    return true;
}

static wy_u64 load_be_(const wy_u8* p, wy_uword width)
{
    wy_u64 value = 0;
    for (wy_uword i = 0; i < width; i++) { value = (value << 8) | p[i]; }
    return value;
}

/** Everything but a tag: majors 0-5 and the five legal major 7 bytes. */
static wy_error read_untagged_(wy_cbor_reader* reader, wy_cbor_item* out)
{
    if (reader->pos >= reader->len) { return WY_ERR_IMAGE; }
    wy_u8 head = reader->data[reader->pos++];
    wy_u8 major = (wy_u8) (head >> 5);
    wy_u8 arg = (wy_u8) (head & 0x1Fu);

    out->n = 0;
    out->data = WY_NULL;
    out->f = 0.0;

    if (major == 7) {
        switch (head) {
        case 0xF4: out->kind = WY_CBOR_FALSE; return WY_ERR_NONE;
        case 0xF5: out->kind = WY_CBOR_TRUE; return WY_ERR_NONE;
        case 0xF6: out->kind = WY_CBOR_NIL; return WY_ERR_NONE;
        case 0xFA: {
            if (remaining_(reader) < 4) { return WY_ERR_IMAGE; }
            wy_u32 bits = (wy_u32) load_be_(&reader->data[reader->pos], 4);
            float value;
            wy_memcpy(&value, &bits, sizeof(value));
            reader->pos += 4;
            out->kind = WY_CBOR_FLOAT32;
            out->f = (double) value;
            return WY_ERR_NONE;
        }
        case 0xFB: {
            if (remaining_(reader) < 8) { return WY_ERR_IMAGE; }
            wy_u64 bits = load_be_(&reader->data[reader->pos], 8);
            double value;
            wy_memcpy(&value, &bits, sizeof(value));
            reader->pos += 8;
            out->kind = WY_CBOR_FLOAT64;
            out->f = value;
            return WY_ERR_NONE;
        }
        default: return WY_ERR_IMAGE;
        }
    }

    wy_u64 n;
    wy_error err = read_arg_(reader, arg, &n);
    if (err != WY_ERR_NONE) { return err; }

    switch (major) {
    case 0:
        out->kind = WY_CBOR_UINT;
        out->n = n;
        return WY_ERR_NONE;
    case 1:
        out->kind = WY_CBOR_NEGINT;
        out->n = n;
        return WY_ERR_NONE;
    case 2:
    case 3:
        if (arg == 27) { return WY_ERR_IMAGE; }  // 64-bit length
        if (n > (wy_u64) remaining_(reader)) { return WY_ERR_IMAGE; }
        out->kind = (major == 2) ? WY_CBOR_BYTES : WY_CBOR_TEXT;
        out->n = n;
        out->data = &reader->data[reader->pos];
        if (major == 3 && !utf8_valid_(out->data, n)) { return WY_ERR_IMAGE; }
        reader->pos += (wy_uword) n;
        return WY_ERR_NONE;
    case 4:
    case 5:
        if (arg == 27) { return WY_ERR_IMAGE; }
        // Every item takes at least one byte: a count the buffer cannot hold
        // is truncated, and rejecting it here keeps a caller from sizing a
        // table from a forged count.
        if (n > (wy_u64) remaining_(reader) / (major == 5 ? 2u : 1u)) { return WY_ERR_IMAGE; }
        out->kind = (major == 4) ? WY_CBOR_ARRAY : WY_CBOR_MAP;
        out->n = n;
        return WY_ERR_NONE;
    default:
        return WY_ERR_IMAGE;
    }
}

wy_error wy_cbor_read(wy_cbor_reader* reader, wy_cbor_item* out)
{
    if (reader == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    if (reader->pos >= reader->len) { return WY_ERR_IMAGE; }

    wy_u8 head = reader->data[reader->pos];
    if ((head >> 5) != 6) { return read_untagged_(reader, out); }

    // Only tags 280 and 282, always as D9 01 18 / D9 01 1A, each wrapping
    // one item of a fixed type.
    reader->pos++;
    wy_u64 tag;
    wy_error err = read_arg_(reader, (wy_u8) (head & 0x1Fu), &tag);
    if (err != WY_ERR_NONE) { return err; }
    if ((head & 0x1Fu) != 25u) { return WY_ERR_IMAGE; }
    if (tag != WY_CBOR_TAG_SYMBOL && tag != WY_CBOR_TAG_CHAR) { return WY_ERR_IMAGE; }
    if (reader->pos >= reader->len) { return WY_ERR_IMAGE; }

    wy_u8 wrapped = (wy_u8) (reader->data[reader->pos] >> 5);
    err = read_untagged_(reader, out);
    if (err != WY_ERR_NONE) { return err; }
    if (tag == WY_CBOR_TAG_SYMBOL) {
        if (wrapped != 3) { return WY_ERR_IMAGE; }
        out->kind = WY_CBOR_SYMBOL;
    } else {
        if (wrapped != 0 || out->n > 0xFFFFFFFFu) { return WY_ERR_IMAGE; }
        out->kind = WY_CBOR_CHAR;
    }
    return WY_ERR_NONE;
}

typedef struct skip_level_
{
    wy_u64 remaining;  // items still to read at this level
    bool is_map;
} skip_level_;

/** A map key must be an integer, string, symbol or byte string. */
static bool key_kind_valid_(wy_cbor_kind kind)
{
    return kind == WY_CBOR_UINT || kind == WY_CBOR_NEGINT || kind == WY_CBOR_BYTES ||
           kind == WY_CBOR_TEXT || kind == WY_CBOR_SYMBOL;
}

wy_error wy_cbor_skip(wy_cbor_reader* reader)
{
    if (reader == WY_NULL) { return WY_ERR_INVAL; }

    skip_level_ stack[WY_CBOR_MAX_DEPTH];
    wy_uword depth = 0;
    do {
        // In a map the items alternate key, value; `remaining` counts down
        // from 2n, so a key is read when it is even.
        bool is_key = depth > 0 && stack[depth - 1].is_map && (stack[depth - 1].remaining % 2u) == 0;
        wy_cbor_item item;
        wy_error err = wy_cbor_read(reader, &item);
        if (err != WY_ERR_NONE) { return err; }
        if (is_key && !key_kind_valid_(item.kind)) { return WY_ERR_IMAGE; }
        if (depth > 0) { stack[depth - 1].remaining--; }

        if ((item.kind == WY_CBOR_ARRAY || item.kind == WY_CBOR_MAP) && item.n > 0) {
            if (depth >= WY_CBOR_MAX_DEPTH) { return WY_ERR_IMAGE; }
            bool is_map = item.kind == WY_CBOR_MAP;
            stack[depth].remaining = is_map ? item.n * 2u : item.n;
            stack[depth].is_map = is_map;
            depth++;
        }
        while (depth > 0 && stack[depth - 1].remaining == 0) { depth--; }
    } while (depth > 0);
    return WY_ERR_NONE;
}

wy_error wy_cbor_validate(const wy_u8* data, wy_uword len)
{
    if (data == WY_NULL) { return WY_ERR_INVAL; }
    wy_cbor_reader reader;
    wy_cbor_reader_init(&reader, data, len);
    wy_error err = wy_cbor_skip(&reader);
    if (err != WY_ERR_NONE) { return err; }
    return wy_cbor_reader_at_end(&reader) ? WY_ERR_NONE : WY_ERR_IMAGE;
}

wy_error wy_cbor_next_span(wy_cbor_reader* reader, wy_cbor_span* out)
{
    if (reader == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }
    wy_uword start = reader->pos;
    wy_error err = wy_cbor_skip(reader);
    if (err != WY_ERR_NONE) { return err; }
    out->data = &reader->data[start];
    out->len = reader->pos - start;
    return WY_ERR_NONE;
}

wy_error wy_cbor_span_item(wy_cbor_span span, wy_cbor_item* out)
{
    if (span.data == WY_NULL) { return WY_ERR_IMAGE; }
    wy_cbor_reader reader;
    wy_cbor_reader_init(&reader, span.data, span.len);
    return wy_cbor_read(&reader, out);
}

static bool span_is_kind_(wy_cbor_span span, wy_cbor_kind kind)
{
    wy_cbor_item item;
    return wy_cbor_span_item(span, &item) == WY_ERR_NONE && item.kind == kind;
}

bool wy_cbor_span_is_array(wy_cbor_span span) { return span_is_kind_(span, WY_CBOR_ARRAY); }
bool wy_cbor_span_is_map(wy_cbor_span span) { return span_is_kind_(span, WY_CBOR_MAP); }

static wy_error open_container_(wy_cbor_span span, wy_cbor_kind kind, wy_cbor_reader* reader, wy_uword* out_count)
{
    if (span.data == WY_NULL || reader == WY_NULL || out_count == WY_NULL) { return WY_ERR_IMAGE; }
    wy_cbor_reader_init(reader, span.data, span.len);
    wy_cbor_item item;
    wy_error err = wy_cbor_read(reader, &item);
    if (err != WY_ERR_NONE) { return err; }
    if (item.kind != kind) { return WY_ERR_IMAGE; }
    *out_count = (wy_uword) item.n;
    return WY_ERR_NONE;
}

wy_error wy_cbor_open_array(wy_cbor_span span, wy_cbor_reader* reader, wy_uword* out_count)
{
    return open_container_(span, WY_CBOR_ARRAY, reader, out_count);
}

wy_error wy_cbor_open_map(wy_cbor_span span, wy_cbor_reader* reader, wy_uword* out_count)
{
    return open_container_(span, WY_CBOR_MAP, reader, out_count);
}

wy_error wy_cbor_map_find(wy_cbor_span span, const char* key, wy_cbor_span* out_value)
{
    wy_cbor_reader reader;
    wy_uword count;
    wy_error err = wy_cbor_open_map(span, &reader, &count);
    if (err != WY_ERR_NONE) { return err; }

    wy_uword key_len = wy_strlen_f(key);
    for (wy_uword i = 0; i < count; i++) {
        wy_cbor_span key_span;
        err = wy_cbor_next_span(&reader, &key_span);
        if (err != WY_ERR_NONE) { return err; }
        wy_cbor_item key_item;
        err = wy_cbor_span_item(key_span, &key_item);
        if (err != WY_ERR_NONE) { return err; }
        if (!key_kind_valid_(key_item.kind)) { return WY_ERR_IMAGE; }

        wy_cbor_span value;
        err = wy_cbor_next_span(&reader, &value);
        if (err != WY_ERR_NONE) { return err; }
        if (key_item.kind == WY_CBOR_TEXT && key_item.n == key_len &&
            wy_memcmp(key_item.data, key, key_len) == 0) {
            *out_value = value;
            return WY_ERR_NONE;
        }
    }
    return WY_ERR_KEY;
}

wy_error wy_cbor_get_u64(wy_cbor_span span, wy_u64* out)
{
    wy_cbor_item item;
    wy_error err = wy_cbor_span_item(span, &item);
    if (err != WY_ERR_NONE || item.kind != WY_CBOR_UINT) { return WY_ERR_IMAGE; }
    *out = item.n;
    return WY_ERR_NONE;
}

wy_error wy_cbor_get_u32(wy_cbor_span span, wy_u32* out)
{
    wy_u64 value;
    wy_error err = wy_cbor_get_u64(span, &value);
    if (err != WY_ERR_NONE || value > 0xFFFFFFFFu) { return WY_ERR_IMAGE; }
    *out = (wy_u32) value;
    return WY_ERR_NONE;
}

wy_error wy_cbor_get_i64(wy_cbor_span span, wy_i64* out)
{
    wy_cbor_item item;
    wy_error err = wy_cbor_span_item(span, &item);
    if (err != WY_ERR_NONE) { return WY_ERR_IMAGE; }
    if (item.kind == WY_CBOR_UINT && item.n <= (wy_u64) INT64_MAX) {
        *out = (wy_i64) item.n;
        return WY_ERR_NONE;
    }
    if (item.kind == WY_CBOR_NEGINT && item.n <= (wy_u64) INT64_MAX) {
        *out = -1 - (wy_i64) item.n;
        return WY_ERR_NONE;
    }
    return WY_ERR_IMAGE;
}

wy_error wy_cbor_get_i32(wy_cbor_span span, wy_i32* out)
{
    wy_i64 value;
    wy_error err = wy_cbor_get_i64(span, &value);
    if (err != WY_ERR_NONE || value < INT32_MIN || value > INT32_MAX) { return WY_ERR_IMAGE; }
    *out = (wy_i32) value;
    return WY_ERR_NONE;
}

wy_error wy_cbor_get_bool(wy_cbor_span span, bool* out)
{
    wy_cbor_item item;
    wy_error err = wy_cbor_span_item(span, &item);
    if (err != WY_ERR_NONE) { return WY_ERR_IMAGE; }
    if (item.kind == WY_CBOR_TRUE) { *out = true; return WY_ERR_NONE; }
    if (item.kind == WY_CBOR_FALSE) { *out = false; return WY_ERR_NONE; }
    return WY_ERR_IMAGE;
}

wy_error wy_cbor_get_text(wy_cbor_span span, const char** out_text, wy_uword* out_len)
{
    wy_cbor_item item;
    wy_error err = wy_cbor_span_item(span, &item);
    if (err != WY_ERR_NONE || item.kind != WY_CBOR_TEXT) { return WY_ERR_IMAGE; }
    *out_text = (const char*) item.data;
    *out_len = (wy_uword) item.n;
    return WY_ERR_NONE;
}

wy_error wy_cbor_get_bytes(wy_cbor_span span, const wy_u8** out_data, wy_uword* out_len)
{
    wy_cbor_item item;
    wy_error err = wy_cbor_span_item(span, &item);
    if (err != WY_ERR_NONE || item.kind != WY_CBOR_BYTES) { return WY_ERR_IMAGE; }
    *out_data = item.data;
    *out_len = (wy_uword) item.n;
    return WY_ERR_NONE;
}
