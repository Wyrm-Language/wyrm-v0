#ifndef WYRM_BSON_H_
#define WYRM_BSON_H_

#include <wyrm/sys/string.h>
#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>

WY_BEGIN_DECLS


/**
 * A lightweight BSON reader.
 *
 * See spec:
 * https://bsonspec.org/spec.html
 */
typedef struct wy_bson_doc_reader
{
    const wy_u8* data;
    wy_uword data_len;
    wy_uword pos;
} wy_bson_doc_reader;

/**
 * A wy_bson_doc_reader restricted to array documents (index-keyed: "0",
 * "1", "2", ...). Identical layout and semantics; the type exists only so
 * wy_bson_array_reader_get can drop the (ignored) key out-parameter.
 */
typedef wy_bson_doc_reader wy_bson_array_reader;

/**
 * The pinned eight-type BSON subset (pypoc/doc/wyc-format.md §4.2). A
 * reader for a .wyc section MUST reject any other tag with WY_ERR_IMAGE,
 * and binary subtype other than 0.
 */
enum
{
    WY_BSON_TAG_DOUBLE = 1,         // 64bit double (double)
    WY_BSON_TAG_STRING = 2,         // UTF-8 string (string)
    WY_BSON_TAG_DOCUMENT = 3,       // nested document (document)
    WY_BSON_TAG_ARRAY = 4,          // array (document)
    WY_BSON_TAG_BINARY = 5,         // binary data, subtype 0 only
    WY_BSON_TAG_BOOL = 8,           // Boolean, byte, 0 = False, 1 = True
    WY_BSON_TAG_NIL = 10,           // Null value
    WY_BSON_TAG_I32 = 16,           // 32-bit integer
};

WY_INLINE wy_i32 wy_bson_get_i32(const wy_u8* buffer) { wy_i32 v; wy_memcpy(&v, buffer, sizeof(wy_i32)); return v; }
WY_INLINE wy_i64 wy_bson_get_i64(const wy_u8* buffer) { wy_i64 v; wy_memcpy(&v, buffer, sizeof(wy_i64)); return v; }
WY_INLINE wy_u64 wy_bson_get_u64(const wy_u8* buffer) { wy_u64 v; wy_memcpy(&v, buffer, sizeof(wy_u64)); return v; }

WY_INLINE void wy_bson_get_str(const wy_u8* buffer, const char** out_str, wy_uword* out_len)
{
    wy_i32 len = wy_bson_get_i32(buffer);
    *out_str = (const char*) &buffer[4];
    *out_len = len;
}

/**
 * Get BSON bianry data
 *
 * @param buffer Input buffer
 * @param sz Size of the input buffer
 * @param out_buffer Output data buffer
 * @param out_subtype Output data subtype
 * @param out_len Output data size in bytes
 * @return WY_ERR_NONE or appropriate error code
 */
WY_INLINE wy_error wy_bson_get_binary_f(const wy_u8* buffer, wy_uword sz, const wy_u8** out_buffer, wy_u8* out_subtype, wy_uword* out_len)
{
    if (sz < 5) { return WY_ERR_INVAL; }
    wy_i32 decode_len = wy_bson_get_i32(buffer);
    if (decode_len < 0 || sz < ((wy_uword)decode_len + 5)) { return WY_ERR_INVAL; }

    *out_buffer = &buffer[5];
    *out_subtype = buffer[4];
    *out_len = (wy_uword) decode_len;
    return WY_ERR_NONE;
}


/**
 * Get a BSON double (tag 1: 8 bytes, IEEE-754 binary64).
 *
 * @param buffer Field value bytes
 * @param sz Bytes available at buffer
 * @param out_value Decoded value
 * @return WY_ERR_NONE, or WY_ERR_IMAGE if fewer than 8 bytes are available
 */
WY_INLINE wy_error wy_bson_get_double_f(const wy_u8* buffer, wy_uword sz, double* out_value)
{
    if (sz < sizeof(double)) { return WY_ERR_IMAGE; }
    wy_memcpy(out_value, buffer, sizeof(double));
    return WY_ERR_NONE;
}

WY_INLINE bool wy_bson_get_bool(const wy_u8* buffer) { return buffer[0] != 0; }

wy_error wy_bson_doc_reader_start(wy_bson_doc_reader* reader, const wy_u8* data, wy_uword data_len);

wy_error wy_bson_doc_reader_get(wy_bson_doc_reader* reader,
    wy_u8* out_tag_type,
    const char** out_cstr,
    const wy_u8** out_buffer,
    wy_uword* out_length);

wy_error wy_bson_doc_reader_find(wy_bson_doc_reader* reader, const char* name,
    wy_u8* out_tag_type,
    const wy_u8** out_buffer,
    wy_uword* out_length);

/**
 * Like wy_bson_doc_reader_start, for a document whose keys are the
 * decimal indices "0", "1", "2", ... (pypoc/doc/wyc-format.md §4.1).
 */
WY_INLINE wy_error wy_bson_array_reader_start(wy_bson_array_reader* reader, const wy_u8* data, wy_uword data_len)
{
    return wy_bson_doc_reader_start(reader, data, data_len);
}

/**
 * Like wy_bson_doc_reader_get, for a wy_bson_array_reader: the element's
 * index key is not returned, since a reader MAY ignore it (§4.1).
 */
WY_INLINE wy_error wy_bson_array_reader_get(wy_bson_array_reader* reader,
    wy_u8* out_tag_type,
    const wy_u8** out_buffer,
    wy_uword* out_length)
{
    const char* ignored_key;
    return wy_bson_doc_reader_get(reader, out_tag_type, &ignored_key, out_buffer, out_length);
}

WY_END_DECLS

#endif
