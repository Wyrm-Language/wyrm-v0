#ifndef WYRM_CBOR_H_
#define WYRM_CBOR_H_

#include <wyrm/sys/string.h>
#include <wyrm/sys/toolchain.h>
#include <wyrm/sys/errors.h>

WY_BEGIN_DECLS

/**
 * A strict reader for the Wyrm wire encoding, a subset of CBOR (RFC 8949):
 * project/design/wire_encoding.md. Every module image section is read through
 * it. The reader rejects, with WY_ERR_IMAGE, everything outside the subset:
 * non-shortest arguments, indefinite lengths, 64-bit lengths, float16,
 * undefined, unused simple values, tags other than 280 (symbol) and 282
 * (char), invalid UTF-8, and map keys that are not an integer, string,
 * symbol or byte string.
 *
 * Nothing here recurses: a container is walked with an explicit stack of at
 * most WY_CBOR_MAX_DEPTH levels.
 */

/** Deepest container nesting wy_cbor_skip accepts. */
#define WY_CBOR_MAX_DEPTH 32

typedef enum wy_cbor_kind
{
    WY_CBOR_UINT = 0,   // n = value
    WY_CBOR_NEGINT,     // n = N, the value is -1 - N
    WY_CBOR_BYTES,      // data/n = body
    WY_CBOR_TEXT,       // data/n = UTF-8 body
    WY_CBOR_ARRAY,      // n = item count; the items follow
    WY_CBOR_MAP,        // n = pair count; the 2n items follow
    WY_CBOR_SYMBOL,     // tag 280: data/n = UTF-8 body
    WY_CBOR_CHAR,       // tag 282: n = code point (any u32)
    WY_CBOR_FALSE,
    WY_CBOR_TRUE,
    WY_CBOR_NIL,
    WY_CBOR_FLOAT32,   // f = the value widened to double
    WY_CBOR_FLOAT64,   // f = the value
} wy_cbor_kind;

typedef struct wy_cbor_item
{
    wy_cbor_kind kind;
    wy_u64 n;
    const wy_u8* data;
    double f;
} wy_cbor_item;

/** A cursor over a buffer. */
typedef struct wy_cbor_reader
{
    const wy_u8* data;
    wy_uword len;
    wy_uword pos;
} wy_cbor_reader;

/** The complete bytes of one item: header and body, children included. */
typedef struct wy_cbor_span
{
    const wy_u8* data;
    wy_uword len;
} wy_cbor_span;

WY_INLINE void wy_cbor_reader_init(wy_cbor_reader* reader, const wy_u8* data, wy_uword len)
{
    reader->data = data;
    reader->len = len;
    reader->pos = 0;
}

WY_INLINE bool wy_cbor_reader_at_end(const wy_cbor_reader* reader) { return reader->pos >= reader->len; }

/**
 * Read one item header (and the body of a byte string, text string, symbol or
 * char). For an array or a map `n` is the count and the reader is left at its
 * first child. Never reads past `len`.
 *
 * @return WY_ERR_NONE, or WY_ERR_IMAGE for truncated or out-of-subset input
 */
wy_error wy_cbor_read(wy_cbor_reader* reader, wy_cbor_item* out);

/**
 * Skip one whole item, children included, validating every item in it.
 * Iterative; WY_ERR_IMAGE if it nests deeper than WY_CBOR_MAX_DEPTH.
 */
wy_error wy_cbor_skip(wy_cbor_reader* reader);

/**
 * Validate a buffer that holds exactly one item and nothing after it
 * (wire_encoding.md Canonical rules, 7).
 */
wy_error wy_cbor_validate(const wy_u8* data, wy_uword len);

/** Skip one item and report its bytes. */
wy_error wy_cbor_next_span(wy_cbor_reader* reader, wy_cbor_span* out);

/** Read the first item of `span`, validating nothing else. */
wy_error wy_cbor_span_item(wy_cbor_span span, wy_cbor_item* out);

/** True if `span` holds an array or a map (without validating the children). */
bool wy_cbor_span_is_array(wy_cbor_span span);
bool wy_cbor_span_is_map(wy_cbor_span span);

/**
 * Position `reader` at the first element of the array in `span` and answer its
 * element count. WY_ERR_IMAGE if `span` is not an array. Read the elements
 * with wy_cbor_next_span.
 */
wy_error wy_cbor_open_array(wy_cbor_span span, wy_cbor_reader* reader, wy_uword* out_count);

/**
 * As wy_cbor_open_array for a map: `out_count` is the number of pairs, and
 * the reader yields key, value, key, value, ...
 */
wy_error wy_cbor_open_map(wy_cbor_span span, wy_cbor_reader* reader, wy_uword* out_count);

/**
 * Find the value stored under a text key in the map in `span`.
 *
 * @return WY_ERR_NONE if found, WY_ERR_KEY if the map has no such key,
 *   WY_ERR_IMAGE if `span` is not a map or is malformed
 */
wy_error wy_cbor_map_find(wy_cbor_span span, const char* key, wy_cbor_span* out_value);

/** Typed getters: WY_ERR_IMAGE unless `span` is exactly the requested type and range. */
wy_error wy_cbor_get_u64(wy_cbor_span span, wy_u64* out);
wy_error wy_cbor_get_u32(wy_cbor_span span, wy_u32* out);
wy_error wy_cbor_get_i64(wy_cbor_span span, wy_i64* out);
wy_error wy_cbor_get_i32(wy_cbor_span span, wy_i32* out);
wy_error wy_cbor_get_bool(wy_cbor_span span, bool* out);
/** Text string body, not NUL terminated. */
wy_error wy_cbor_get_text(wy_cbor_span span, const char** out_text, wy_uword* out_len);
wy_error wy_cbor_get_bytes(wy_cbor_span span, const wy_u8** out_data, wy_uword* out_len);

WY_END_DECLS

#endif
