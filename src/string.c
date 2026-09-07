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


const wy_object_type wy_string_type = {
   .object = WY_OBJECT_TYPE_OBJECT_INIT,
   .gc_type = WY_TYPE_TAG_STR,

   .finalize = finalize_f,
};
