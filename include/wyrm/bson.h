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

enum
{
    WY_BSON_TAG_DOUBLE = 1,         // 64bit double (double)
    WY_BSON_TAG_STRING = 2,         // UTF-8 string (string)
    WY_BSON_TAG_DOCUMENT = 3,       // nested document (document)
    WY_BSON_TAG_ARRAY = 4,          // array (document)
    WY_BSON_TAG_BINARY = 5,         // binary data
    WY_BSON_TAG_RESERVED_1 = 6,     // undefined (value).
    WY_BSON_TAG_OID = 7,            // Object Id (12 bytes)
    WY_BSON_TAG_BOOL = 8,           // Boolean, byte, 0 = False, 1 = True
    WY_BSON_TAG_DTIME = 9,          // Date-Time UTC milliseconds since unich epoch int64
    WY_BSON_TAG_NIL = 10,           // Null value
    WY_BSON_TAG_CSTRING = 11,       // C-STRING
    WY_BSON_TAG_RESERVED_2 = 12,    // DBPointer, deprecated
    WY_BSON_TAG_JS_CODE = 13,       // JSON code
    WY_BSON_TAG_RESERVED_3 = 14,    // Symbol. deprecated
    WY_BSON_TAG_RESERVED_4 = 15,    // Javascript code with scope, deprecated
    WY_BSON_TAG_I32 = 16,           // 32-bit integer
    WY_BSON_TAG_U64 = 17,           // 64-bit unsigned integer
    WY_BSON_TAG_I64 = 18,           // 64-bit signed integer
    WY_BSON_TAG_D128 = 19,          // Decimal128
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

WY_END_DECLS

#endif
