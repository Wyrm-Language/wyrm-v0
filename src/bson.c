#include <wyrm/bson.h>

static wy_i32 flength_(wy_uword pos, const wy_u8* data, wy_uword data_len, wy_u8 tag_type)
{
    wy_uword cur_pos = pos;
    wy_i32 len;

    switch (tag_type) {
    case WY_BSON_TAG_DOCUMENT:
    case WY_BSON_TAG_ARRAY:
        if (pos + sizeof(wy_i32) > data_len) { return -1; }
        return wy_bson_get_i32(&data[pos]);

    case WY_BSON_TAG_STRING:
        if (pos + sizeof(wy_i32) > data_len) { return -1; }
        len = wy_bson_get_i32(&data[pos]);
        len += 4;
        return len;

    case WY_BSON_TAG_BINARY:
        if (pos + sizeof(wy_i32) > data_len) { return -1; }
        len = wy_bson_get_i32(&data[pos]);
        len += 5;
        return len;

    case WY_BSON_TAG_CSTRING:
        for (; cur_pos < data_len && data[cur_pos] != '\0'; cur_pos++) { ; }
        cur_pos++;
        return (wy_i32) (cur_pos - pos);

    case WY_BSON_TAG_DOUBLE:
    case WY_BSON_TAG_I64:
    case WY_BSON_TAG_U64:
        return 8;

    case WY_BSON_TAG_I32:
        return 4;

    case WY_BSON_TAG_BOOL:
        return 1;

    case WY_BSON_TAG_NIL:
        return 0;

    default:
        return -1;
    }
}

/**
 * Initialize a BSON doc reader
 *
 * @param reader Reader data structure to initialize
 * @param data A buffer of binary data
 * @param data_len The allocated size of the buffer
 * @return WYRM_ERR_NONE on success, otherwise an error code
 */
wy_error wy_bson_doc_reader_start(wy_bson_doc_reader* reader, const wy_u8* data, wy_uword data_len)
{
    if (data_len < 4) { return WYRM_ERR_INVAL; }
    wy_i32 doc_len = wy_bson_get_i32(data);
    if (doc_len < 0 || data_len < (wy_uword) doc_len) { return WYRM_ERR_INVAL; }
    reader->data = data;
    reader->pos = 4;
    reader->data_len = (wy_uword) doc_len;
    return WYRM_ERR_NONE;
}

/**
 * Read the next field of a BSON document
 *
 * @param reader Reader to move to next field
 * @param out_tag_type The tag type of the field
 * @param out_cstr The cstring of the field
 * @param out_buffer The buffer of the field
 * @param out_length The length of the field
 * @return WYRM_ERR_NONE on success, WYRM_ERR_STOP_ITERATION if no more fields, otherwise an error code
 */
wy_error wy_bson_doc_reader_get(wy_bson_doc_reader* reader, wy_u8* out_tag_type, const char** out_cstr, const wy_u8** out_buffer, wy_uword* out_length)
{
    wy_uword pos = reader->pos;
    if ((pos + 2) >= reader->data_len) { return WYRM_ERR_STOP_ITERATION; }

    // Identify tag
    wy_u8 tag_type = reader->data[pos++];

    // Find cstring end
    const char* found_cstr = (const char*) &reader->data[pos];
    for (; pos < reader->data_len && reader->data[pos] != '\0'; pos++) { ; }
    if (pos >= reader->data_len) { return WYRM_ERR_INVAL; }
    pos++;  // past the key's own NUL terminator

    wy_uword dstart = pos;
    wy_i32 flength = flength_(pos, reader->data, reader->data_len, tag_type);
    if (flength < 0) { return WYRM_ERR_INVAL; }

    wy_uword new_pos = pos + (wy_uword) flength;
    if (new_pos > reader->data_len || new_pos < pos) { return WYRM_ERR_INVAL; }

    // Advance checks out, do it.
    reader->pos = new_pos;
    *out_tag_type = tag_type;
    *out_cstr = found_cstr;
    *out_buffer = &reader->data[dstart];
    *out_length = (wy_uword) flength;
    return WYRM_ERR_NONE;
}

/**
 * Find the given field in the BSON document.
 *
 * @param reader Reader to leverage find
 * @param name Name of the field to find
 * @param out_tag_type The tag type of the field
 * @param out_buffer Buffer of the field
 * @param out_length Length of the field
 * @return WYRM_ERR_NONE if found, WYRM_ERR_KEY if not found, or error on bad data
 */
wy_error wy_bson_doc_reader_find(wy_bson_doc_reader* reader, const char* name,
                             wy_u8* out_tag_type,
                             const wy_u8** out_buffer,
                             wy_uword* out_length)
{
    wy_bson_doc_reader tmp = *reader;
    wy_u8 cur_tag_type;
    const char* cur_name = WYRM_NULL;
    const wy_u8* cur_buffer = WYRM_NULL;
    wy_uword cur_length = 0;

    wy_error last_error = wy_bson_doc_reader_get(&tmp, &cur_tag_type, &cur_name, &cur_buffer, &cur_length);
    while (last_error == WYRM_ERR_NONE) {
        if (wyrm_strcmp_f(cur_name, name) == 0) {
            *out_tag_type = cur_tag_type;
            *out_buffer = cur_buffer;
            *out_length = cur_length;
            return WYRM_ERR_NONE;
        }
        last_error = wy_bson_doc_reader_get(&tmp, &cur_tag_type, &cur_name, &cur_buffer, &cur_length);
    }

    if (last_error != WYRM_ERR_STOP_ITERATION) { return last_error; }
    return WYRM_ERR_KEY;

}
