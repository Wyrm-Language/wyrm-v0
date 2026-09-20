#include <wyrm/image_loader.h>

#define WY_IMAGE_HEADER_SIZE 8u
#define WY_IMAGE_DIRECTORY_ENTRY_SIZE 12u
#define WY_IMAGE_VERSION 1u

static wy_u32 read_u32_le_(const wy_u8* p)
{
    return (wy_u32) p[0] | ((wy_u32) p[1] << 8) | ((wy_u32) p[2] << 16) | ((wy_u32) p[3] << 24);
}

wy_error wy_image_from_bytes(const wy_u8* data, wy_uword len, wy_module_image* out)
{
    if (data == WY_NULL || out == WY_NULL) { return WY_ERR_INVAL; }

    if (len < WY_IMAGE_HEADER_SIZE) { return WY_ERR_IMAGE; }
    if (data[0] != 'W' || data[1] != 'Y' || data[2] != 'C' || data[3] != 0x00) { return WY_ERR_IMAGE; }
    if (data[4] != WY_IMAGE_VERSION) { return WY_ERR_IMAGE; }

    wy_u8 section_count = data[5];
    // data[6..7] reserved, ignored.

    wy_uword directory_size = (wy_uword) section_count * WY_IMAGE_DIRECTORY_ENTRY_SIZE;
    if (len - WY_IMAGE_HEADER_SIZE < directory_size) { return WY_ERR_IMAGE; }
    wy_uword directory_end = WY_IMAGE_HEADER_SIZE + directory_size;

    out->name = WY_NULL;
    for (wy_uword i = 0; i < WY_SEC_COUNT; i++) {
        out->sections[i].data = WY_NULL;
        out->sections[i].len = 0;
    }

    wy_u8 previous_id = 0;
    for (wy_u8 i = 0; i < section_count; i++) {
        wy_uword entry_offset = WY_IMAGE_HEADER_SIZE + (wy_uword) i * WY_IMAGE_DIRECTORY_ENTRY_SIZE;
        wy_u8 id = data[entry_offset];
        // data[entry_offset + 1 .. 3] reserved, ignored.
        wy_u32 payload_offset = read_u32_le_(&data[entry_offset + 4]);
        wy_u32 payload_length = read_u32_le_(&data[entry_offset + 8]);

        // Recognised ids are 1..WY_SEC_COUNT-1; id 0 is reserved for a
        // future CRC section and, like any other id this v1 loader does
        // not know, must be rejected rather than silently skipped.
        if (id < 1 || id >= WY_SEC_COUNT) { return WY_ERR_IMAGE; }
        // Ascending, unique: catches both an unsorted directory and a
        // duplicate id in one comparison.
        if (id <= previous_id) { return WY_ERR_IMAGE; }
        previous_id = id;

        // A payload may not start before the directory ends (it would
        // overlap the header/directory it is described by) or run past the
        // end of the file.
        if ((wy_uword) payload_offset < directory_end) { return WY_ERR_IMAGE; }
        if ((wy_uword) payload_offset > len) { return WY_ERR_IMAGE; }
        if ((wy_uword) payload_length > len - (wy_uword) payload_offset) { return WY_ERR_IMAGE; }
        if (payload_offset % 4 != 0) { return WY_ERR_IMAGE; }
        if (id == WY_SEC_CODE && payload_length % 4 != 0) { return WY_ERR_IMAGE; }

        out->sections[id].data = &data[payload_offset];
        out->sections[id].len = payload_length;
    }

    if (out->sections[WY_SEC_HEADER].data == WY_NULL) { return WY_ERR_IMAGE; }
    if (out->sections[WY_SEC_CODE].data == WY_NULL) { return WY_ERR_IMAGE; }

    return WY_ERR_NONE;
}
