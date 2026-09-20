#ifndef WYRM_IMAGE_LOADER_H_
#define WYRM_IMAGE_LOADER_H_

#include <wyrm/image.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/toolchain.h>

WY_BEGIN_DECLS

/**
 * Parse a `.wyc` container (pypoc/doc/wyc-format.md §2) into section
 * references, zero-copy: every wy_section_ref in `out` points directly into
 * `data`, which the caller must keep alive at least as long as `out`.
 *
 * Enforces every container-level rule in §2 and Appendix B step 1: magic,
 * version, ascending unique directory ids, in-bounds and 4-byte-aligned
 * payloads, `code`'s length a multiple of 4, and that `header` (id 1) and
 * `code` (id 8) are present. Does not interpret any section's payload -
 * that is wy_module_load_image's job (epic 1 M4). `debug` (id 9) is
 * recorded in `out->sections` like any other section but never required.
 *
 * `out->name` is left WY_NULL; it is set once `header` is parsed.
 *
 * @param data Whole file contents
 * @param len Byte length of data
 * @param out Populated on success; left in an unspecified state on failure
 * @return WY_ERR_NONE, WY_ERR_INVAL for a null argument, or WY_ERR_IMAGE
 *   for any rule violation
 */
wy_error wy_image_from_bytes(const wy_u8* data, wy_uword len, wy_module_image* out);

WY_END_DECLS

#endif
