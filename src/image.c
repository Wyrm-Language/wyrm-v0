#include <wyrm/cbor.h>
#include <wyrm/image_loader.h>

// The 11-byte preamble: array(2), text "WYCR", uint32 version. The first 7
// bytes are fixed; the last four are the big-endian version.
#define WY_IMAGE_PREAMBLE_SIZE 11u
#define WY_IMAGE_MAGIC_SIZE 7u

static const wy_u8 image_magic_[WY_IMAGE_MAGIC_SIZE] = { 0x82, 0x64, 'W', 'Y', 'C', 'R', 0x1A };

/** Which CBOR type each section's value must be. */
static bool section_kind_ok_(wy_u8 id, wy_cbor_kind kind)
{
    switch (id) {
    case WY_SEC_STATICS:
    case WY_SEC_SYMBOLS:
    case WY_SEC_FUNCTIONS:
    case WY_SEC_CLASSES:
    case WY_SEC_MESSAGES:
        return kind == WY_CBOR_ARRAY;
    case WY_SEC_CODE:
        return kind == WY_CBOR_BYTES;
    default:
        return kind == WY_CBOR_MAP;
    }
}

wy_error wy_image_from_bytes(const wy_u8* data, wy_uword len, wy_module_image* out)
{
    if (data == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    if (len < WY_IMAGE_PREAMBLE_SIZE) { return WY_ERR_IMAGE; }
    if (wy_memcmp(data, image_magic_, WY_IMAGE_MAGIC_SIZE) != 0) { return WY_ERR_IMAGE; }
    wy_u32 version = ((wy_u32) data[7] << 24) | ((wy_u32) data[8] << 16) | ((wy_u32) data[9] << 8) | (wy_u32) data[10];
    if (version != WY_IMAGE_VERSION) { return WY_ERR_IMAGE; }

    out->name = WY_NULL;
    for (wy_uword i = 0; i < WY_SEC_COUNT; i++) {
        out->sections[i].data = WY_NULL;
        out->sections[i].len = 0;
    }

    // The rest of the file is one map: section id -> section value.
    wy_cbor_reader reader;
    wy_cbor_reader_init(&reader, data + WY_IMAGE_PREAMBLE_SIZE, len - WY_IMAGE_PREAMBLE_SIZE);
    wy_cbor_item item;
    wy_error last_error = wy_cbor_read(&reader, &item);
    if (last_error != WY_ERR_NONE || item.kind != WY_CBOR_MAP) { return WY_ERR_IMAGE; }
    wy_u64 pairs = item.n;

    wy_u8 previous_id = 0;
    for (wy_u64 i = 0; i < pairs; i++) {
        last_error = wy_cbor_read(&reader, &item);
        if (last_error != WY_ERR_NONE || item.kind != WY_CBOR_UINT) { return WY_ERR_IMAGE; }
        // Recognised ids are 1..WY_SEC_COUNT-1; any other id, including 0,
        // is rejected rather than silently skipped.
        if (item.n < 1 || item.n >= WY_SEC_COUNT) { return WY_ERR_IMAGE; }
        wy_u8 id = (wy_u8) item.n;
        // Ascending, unique: catches both an unsorted map and a duplicate id.
        if (id <= previous_id) { return WY_ERR_IMAGE; }
        previous_id = id;

        // wy_cbor_next_span validates every item inside the value.
        wy_cbor_span span;
        last_error = wy_cbor_next_span(&reader, &span);
        if (last_error != WY_ERR_NONE) { return WY_ERR_IMAGE; }
        last_error = wy_cbor_span_item(span, &item);
        if (last_error != WY_ERR_NONE || !section_kind_ok_(id, item.kind)) { return WY_ERR_IMAGE; }

        if (id == WY_SEC_CODE) {
            // The section is the words themselves, not the byte string around them.
            if (item.n % 4 != 0) { return WY_ERR_IMAGE; }
            span.data = item.data;
            span.len = (wy_uword) item.n;
        }
        if (span.len > UINT32_MAX) { return WY_ERR_IMAGE; }
        out->sections[id].data = span.data;
        out->sections[id].len = (uint32_t) span.len;
    }

    // Nothing follows the map.
    if (!wy_cbor_reader_at_end(&reader)) { return WY_ERR_IMAGE; }
    if (out->sections[WY_SEC_HEADER].data == WY_NULL) { return WY_ERR_IMAGE; }
    if (out->sections[WY_SEC_CODE].data == WY_NULL) { return WY_ERR_IMAGE; }

    return WY_ERR_NONE;
}
