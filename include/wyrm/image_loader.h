#ifndef WYRM_IMAGE_LOADER_H_
#define WYRM_IMAGE_LOADER_H_

#include <wyrm/image.h>
#include <wyrm/sys/errors.h>
#include <wyrm/sys/toolchain.h>

WY_BEGIN_DECLS

/**
 * Parse a `.wyc` container (doc-llm/llm-bytecode.md §5.3) into section
 * references, zero-copy: every wy_section_ref in `out` points directly into
 * `data`, which the caller must keep alive at least as long as `out`.
 *
 * A file is the 11-byte preamble (array(2), "WYCR", the format version as a
 * uint32) followed by one CBOR map from section id to section value. This
 * rejects any version but WY_IMAGE_VERSION (it never reads an old or unknown
 * format), a map that is not ascending unique known ids, a section of the wrong
 * CBOR type, anything in the file outside the wire subset (the whole file is
 * validated with wy_cbor_skip), bytes after the map, `code` whose length is
 * not a multiple of 4, and a file without `header` (id 1) or `code` (id 8).
 * A structured section's reference spans its CBOR item; `code`'s is the
 * bytes inside its byte string, which need not be 4-byte aligned in memory
 * (wy_module_load_image copies them when they are not). Does not interpret
 * any section's contents - that is wy_module_load_image's job. `debug` (id 9)
 * is recorded in `out->sections` like any other section but never required.
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
