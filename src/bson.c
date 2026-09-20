#include <wyrm/bson.h>

/**
 * Length of a field's value, in bytes, for one of the eight permitted tags
 * (pypoc/doc/wyc-format.md §4.2). -1 means truncated; -2 means the tag
 * itself is not one of the eight - the caller tells the two apart because
 * WY_ERR_IMAGE covers both, but a test can still assert on which check hit.
 */
static wy_i32 flength_(wy_uword pos, const wy_u8* data, wy_uword data_len, wy_u8 tag_type)
{
    wy_i32 len;

    switch (tag_type) {
    case WY_BSON_TAG_DOCUMENT:
    case WY_BSON_TAG_ARRAY:
        if (pos + sizeof(wy_i32) > data_len) { return -1; }
        len = wy_bson_get_i32(&data[pos]);
        if (len < 4) { return -1; }
        return len;

    case WY_BSON_TAG_STRING:
        if (pos + sizeof(wy_i32) > data_len) { return -1; }
        len = wy_bson_get_i32(&data[pos]);
        if (len < 1) { return -1; }
        return len + 4;

    case WY_BSON_TAG_BINARY:
        if (pos + sizeof(wy_i32) > data_len) { return -1; }
        len = wy_bson_get_i32(&data[pos]);
        if (len < 0) { return -1; }
        return len + 5;

    case WY_BSON_TAG_DOUBLE:
        return 8;

    case WY_BSON_TAG_I32:
        return 4;

    case WY_BSON_TAG_BOOL:
        return 1;

    case WY_BSON_TAG_NIL:
        return 0;

    default:
        // Not one of the eight permitted tags (§4.2): BSON defines more,
        // and a conforming reader MUST reject every one of them.
        return -2;
    }
}

/**
 * Extra per-tag validity a length alone cannot express: a bool byte that is
 * neither 0 nor 1, or a binary subtype other than 0. `value` points at the
 * field's value bytes (after the key), `length` is what flength_ returned.
 */
static bool field_value_valid_(wy_u8 tag_type, const wy_u8* value, wy_i32 length)
{
    switch (tag_type) {
    case WY_BSON_TAG_BOOL:
        return value[0] == 0x00 || value[0] == 0x01;
    case WY_BSON_TAG_BINARY:
        // length counts [i32 len][u8 subtype][bytes]; subtype is at index 4.
        return length >= 5 && value[4] == 0x00;
    default:
        return true;
    }
}

/**
 * Initialize a BSON doc reader
 *
 * @param reader Reader data structure to initialize
 * @param data A buffer of binary data
 * @param data_len The allocated size of the buffer
 * @return WY_ERR_NONE on success, otherwise an error code
 */
wy_error wy_bson_doc_reader_start(wy_bson_doc_reader* reader, const wy_u8* data, wy_uword data_len)
{
    if (data_len < 5) { return WY_ERR_IMAGE; }  // shortest legal document is "05 00 00 00 00"
    wy_i32 doc_len = wy_bson_get_i32(data);
    if (doc_len < 5 || data_len < (wy_uword) doc_len) { return WY_ERR_IMAGE; }
    reader->data = data;
    reader->pos = 4;
    reader->data_len = (wy_uword) doc_len;
    return WY_ERR_NONE;
}

/**
 * Read the next field of a BSON document
 *
 * @param reader Reader to move to next field
 * @param out_tag_type The tag type of the field
 * @param out_cstr The cstring of the field
 * @param out_buffer The buffer of the field
 * @param out_length The length of the field
 * @return WY_ERR_NONE on success, WY_ERR_STOP_ITERATION if no more fields, otherwise an error code
 */
wy_error wy_bson_doc_reader_get(wy_bson_doc_reader* reader, wy_u8* out_tag_type, const char** out_cstr, const wy_u8** out_buffer, wy_uword* out_length)
{
    wy_uword pos = reader->pos;
    if (pos >= reader->data_len) { return WY_ERR_IMAGE; }

    // End-of-document is *only* the terminator as the declared length's
    // last byte; anything else that looks like it is malformed rather than
    // an early, silently-accepted stop.
    if (reader->data[pos] == 0x00) {
        if (pos != reader->data_len - 1) { return WY_ERR_IMAGE; }
        reader->pos = reader->data_len;
        return WY_ERR_STOP_ITERATION;
    }

    // Identify tag
    wy_u8 tag_type = reader->data[pos++];

    // Find cstring end
    const char* found_cstr = (const char*) &reader->data[pos];
    for (; pos < reader->data_len && reader->data[pos] != '\0'; pos++) { ; }
    if (pos >= reader->data_len) { return WY_ERR_IMAGE; }
    pos++;  // past the key's own NUL terminator

    wy_uword dstart = pos;
    wy_i32 flength = flength_(pos, reader->data, reader->data_len, tag_type);
    if (flength < 0) { return WY_ERR_IMAGE; }

    wy_uword new_pos = pos + (wy_uword) flength;
    if (new_pos > reader->data_len || new_pos < pos) { return WY_ERR_IMAGE; }

    if (!field_value_valid_(tag_type, &reader->data[dstart], flength)) { return WY_ERR_IMAGE; }

    // Advance checks out, do it.
    reader->pos = new_pos;
    *out_tag_type = tag_type;
    *out_cstr = found_cstr;
    *out_buffer = &reader->data[dstart];
    *out_length = (wy_uword) flength;
    return WY_ERR_NONE;
}

/**
 * Find the given field in the BSON document.
 *
 * @param reader Reader to leverage find
 * @param name Name of the field to find
 * @param out_tag_type The tag type of the field
 * @param out_buffer Buffer of the field
 * @param out_length Length of the field
 * @return WY_ERR_NONE if found, WY_ERR_KEY if not found, or error on bad data
 */
wy_error wy_bson_doc_reader_find(wy_bson_doc_reader* reader, const char* name,
                             wy_u8* out_tag_type,
                             const wy_u8** out_buffer,
                             wy_uword* out_length)
{
    wy_bson_doc_reader tmp = *reader;
    wy_u8 cur_tag_type;
    const char* cur_name = WY_NULL;
    const wy_u8* cur_buffer = WY_NULL;
    wy_uword cur_length = 0;

    wy_error last_error = wy_bson_doc_reader_get(&tmp, &cur_tag_type, &cur_name, &cur_buffer, &cur_length);
    while (last_error == WY_ERR_NONE) {
        if (wy_strcmp_f(cur_name, name) == 0) {
            *out_tag_type = cur_tag_type;
            *out_buffer = cur_buffer;
            *out_length = cur_length;
            return WY_ERR_NONE;
        }
        last_error = wy_bson_doc_reader_get(&tmp, &cur_tag_type, &cur_name, &cur_buffer, &cur_length);
    }

    if (last_error != WY_ERR_STOP_ITERATION) { return last_error; }
    return WY_ERR_KEY;

}
