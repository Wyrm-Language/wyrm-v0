#ifndef WYRM_BYTES_H_
#define WYRM_BYTES_H_

#include <wyrm/object.h>

WY_BEGIN_DECLS

struct wy_bytes;
#ifndef __cplusplus
typedef struct wy_bytes wy_bytes;
#endif

extern const wy_object_type wy_bytes_type;

/**
 * A resizable buffer of raw bytes. Backing heap object for the future
 * `bytes` language type (epic 7 gives it language surface).
 */
struct wy_bytes
{
    wy_object object;
    wy_uword len;
    wy_uword capacity;
    wy_u8* data;
};

/**
 * Allocate a byte buffer holding a copy of `data[0..len)`.
 *
 * `data` may be WY_NULL when `len` is 0.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument (when len > 0 and
 *   data is null, or out is null), or WY_ERR_NOMEM
 */
wy_error wy_bytes_new(wy_context* context, const wy_u8* data, wy_uword len, wy_bytes** out);

/**
 * Grow `self` so that it can hold at least `min_capacity` bytes.
 *
 * Never shrinks the buffer.
 *
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_NOMEM
 */
wy_error wy_bytes_reserve(wy_context* context, wy_bytes* self, wy_uword min_capacity);

WY_END_DECLS

#endif
