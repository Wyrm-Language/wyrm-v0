#include <wyrm/string.h>
#include <wyrm/context.h>
#include <wyrm/sys/string.h>


static void finalize_f(wy_context* context, wy_object* object)
{
   wy_string* self = (wy_string*) object;

   wy_context_gc_free(context, (void*) self->str);
   self->str = WY_NULL;
   self->len = 0;
   self->hash = 0;
}

/**
 * Copy string into VM
 */
wy_error wy_string_new(wy_context* context, const char* src, wy_uword src_len, wy_string** out_str)
{
   wy_error last_error = WY_ERR_NONE;
   if (context == WY_NULL || out_str == WY_NULL) { return WY_ERR_INVAL; }
   if (src == WY_NULL && src_len > 0) { return WY_ERR_INVAL; }

   char* buffer = wy_context_gc_alloc(context, sizeof(char) * (src_len + 1));
   wy_string* new_str = wy_context_gc_alloc(context, sizeof(wy_string));
   if (buffer == WY_NULL || new_str == WY_NULL) { last_error = WY_ERR_NOMEM; goto err_out; }

   if (src != WY_NULL) { wy_memcpy(buffer, src, src_len); }
   buffer[src_len] = '\0';
   wy_context_object_init_header_f(context, &new_str->object, &wy_string_type);

   new_str->str = buffer;
   new_str->hash = wy_hash_buffer(buffer, buffer + src_len);
   new_str->len = src_len;
   *out_str = new_str;
   return WY_ERR_NONE;

   err_out:
      if (new_str != WY_NULL) { wy_context_gc_free(context, new_str); }
   if (buffer != WY_NULL) { wy_context_gc_free(context, buffer); }
   return last_error;
}


/**
 * Duplicate String into the VM
 */
wy_error wy_string_strdup(wy_context* context, const char* src, wy_string** out_str)
{
   if (src == WY_NULL || out_str == WY_NULL) { return WY_ERR_INVAL; }
   return wy_string_new(context, src, wy_strlen_f(src), out_str);
}


wy_error wy_string_concat(wy_context* context, wy_string* lhs, wy_string* rhs, wy_string** out_str)
{
   if (context == WY_NULL || lhs == WY_NULL || rhs == WY_NULL || out_str == WY_NULL) { return WY_ERR_INVAL; }

   wy_uword total = lhs->len + rhs->len;
   char* buffer = wy_context_gc_alloc(context, sizeof(char) * (total + 1));
   wy_string* new_str = wy_context_gc_alloc(context, sizeof(wy_string));
   if (buffer == WY_NULL || new_str == WY_NULL) {
      if (new_str != WY_NULL) { wy_context_gc_free(context, new_str); }
      if (buffer != WY_NULL) { wy_context_gc_free(context, buffer); }
      return WY_ERR_NOMEM;
   }

   wy_memcpy(buffer, lhs->str, lhs->len);
   wy_memcpy(buffer + lhs->len, rhs->str, rhs->len);
   buffer[total] = '\0';
   wy_context_object_init_header_f(context, &new_str->object, &wy_string_type);

   new_str->str = buffer;
   new_str->hash = wy_hash_buffer(buffer, buffer + total);
   new_str->len = total;
   *out_str = new_str;
   return WY_ERR_NONE;
}


wy_uword wy_utf8_decode_f(const char* str, wy_uword len, wy_uword offset, wy_u32* out_cp)
{
    wy_u8 lead = (wy_u8) str[offset];
    wy_uword remaining = len - offset;
    wy_uword seq_len;
    wy_u32 cp;

    if ((lead & 0x80) == 0x00) { seq_len = 1; cp = lead; }
    else if ((lead & 0xE0) == 0xC0) { seq_len = 2; cp = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { seq_len = 3; cp = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { seq_len = 4; cp = lead & 0x07; }
    else { *out_cp = lead; return 1; }

    if (seq_len > remaining) { *out_cp = lead; return 1; }

    for (wy_uword i = 1; i < seq_len; i++) {
        wy_u8 cont = (wy_u8) str[offset + i];
        if ((cont & 0xC0) != 0x80) { *out_cp = lead; return 1; }
        cp = (cp << 6) | (cont & 0x3F);
    }

    *out_cp = cp;
    return seq_len;
}


wy_uword wy_utf8_codepoint_count_f(const char* str, wy_uword len)
{
    wy_uword offset = 0, n = 0;
    for (; offset < len; n++) {
        offset += wy_utf8_decode_f(str, len, offset, &(wy_u32){0});
    }
    return n;
}


wy_uword wy_utf8_offset_at_f(const char* str, wy_uword len, wy_uword n)
{
    wy_uword offset = 0, i = 0;
    for (; offset < len && i < n; i++) {
        offset += wy_utf8_decode_f(str, len, offset, &(wy_u32){0});
    }
    return offset;
}


bool wy_value_truthy(wy_value value)
{
    switch (value.type) {
    case WY_TYPE_TAG_NIL:   return false;
    case WY_TYPE_TAG_BOOL:  return value.data.flag;
    case WY_TYPE_TAG_WORD:  return value.data.word != 0;
    case WY_TYPE_TAG_UWORD: return value.data.uword != 0;
    case WY_TYPE_TAG_FLOAT: return value.data.fp != 0.0;
    case WY_TYPE_TAG_ERROR: return value.data.gc_object != WY_NULL;  /* Unset is false */
    case WY_TYPE_TAG_STR:   return value.data.str != WY_NULL && value.data.str->len != 0;
    default:                return true;
    }
}


const wy_object_type wy_string_type = {
   .object = WY_OBJECT_TYPE_OBJECT_INIT,
   .gc_type = WY_TYPE_TAG_STR,

   .finalize = finalize_f,
};
