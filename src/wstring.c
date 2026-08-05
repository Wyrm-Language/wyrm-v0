#include <wyrm.h>

static void finalize_f(wyrm_context* context, wyrm_object* object)
{
   wyrm_string* self = (wyrm_string*) object;

   wyrm_context_gc_free(context, (void*) self->str);
   self->str = WYRM_NULL;
   self->len = 0;
   self->hash = 0;
}


const wyrm_object_type wyrm_type_string = {
   .object = WYRM_OBJECT_TYPE_OBJECT_INIT,
   .gc_type = WYRM_TYPE_TAG_STR,

   .finalize = finalize_f,
};


/**
 * Duplicate String into the VM
 */
wyrm_error wyrm_string_strdup(wyrm_context* context, const char* src, wyrm_string** out_str)
{
   wyrm_error last_error = WYRM_ERR_NONE;
   if (context == WYRM_NULL || src == WYRM_NULL || out_str == WYRM_NULL) { return WYRM_ERR_INVAL; }
   wyrm_uword src_len = wyrm_strlen_f(src);

   char* buffer = wyrm_context_gc_alloc(context, sizeof(char) * (src_len + 1));
   wyrm_string* new_str = wyrm_context_gc_alloc(context, sizeof(wyrm_string));
   if (buffer == WYRM_NULL || new_str == WYRM_NULL) { last_error = WYRM_ERR_NOMEM; goto err_out; }

   wyrm_memcpy(buffer, src, src_len + 1);
   wyrm_context_object_init_header_f(context, &new_str->object, &wyrm_type_string);

   new_str->str = buffer;
   new_str->hash = wyrm_hash_buffer(buffer, buffer + src_len);
   new_str->len = src_len;
   *out_str = new_str;
   return WYRM_ERR_NONE;

err_out:
   if (new_str != WYRM_NULL) { wyrm_context_gc_free(context, new_str); }
   if (buffer != WYRM_NULL) { wyrm_context_gc_free(context, buffer); }
   return last_error;
}
