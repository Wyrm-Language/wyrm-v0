#ifndef WYRM_HOST_H_
#define WYRM_HOST_H_

/*
 * Embedding wyrm: a small facade over the whole runtime (libwyrmhost).
 *
 * A wy_host owns a complete, ready-to-use interpreter - machine, context,
 * standard library, the embedded compiler, import roots - plus one persistent
 * session, so a handful of calls is enough to run code, set and read
 * variables, load scripts, or hand the user a REPL:
 *
 *     wy_host* host;
 *     wy_host_new(NULL, &host);
 *     wy_host_set_int(host, "width", 40);
 *     wy_host_eval(host, "area := width * 2", NULL);
 *     long area;
 *     wy_host_get_int(host, "area", &area);
 *     wy_host_free(host);
 *
 * Everything evaluated in one host shares one namespace (the session; see
 * doc-llm/repl.md for its scoping rules), so a variable set from C is visible to
 * scripts and the other way round. See doc-llm/embedding.md and examples/.
 *
 * Not thread safe: use one host from one thread at a time.
 */

#include <wyrm.h>

#include <stdbool.h>
#include <stdio.h>

WY_BEGIN_DECLS

typedef struct wy_host wy_host;

/** How a host is set up. Zero everything (or call wy_host_config_default). */
typedef struct wy_host_config
{
    /** Directories searched for `import`ed .wy sources (like `wyrm -I`). May be NULL. */
    const char* const* include_paths;
    wy_uword include_count;
    /** Where compiled imports are cached; NULL = a __wycache__ beside each source. */
    const char* cache_dir;
    /** Where `print`/`println` output goes; NULL = standard output. */
    wy_io_write_fn output;
    void* output_ud;
    /** Code reservation of the session in 32-bit words; 0 = the default (16 MiB). */
    wy_uword code_words;
    /** One stderr line per import resolution (`wyrm -v`). */
    bool verbose;
} wy_host_config;

void wy_host_config_default(wy_host_config* config);

/**
 * Create a host. `config` may be NULL for the defaults.
 * @return WY_ERR_NONE, WY_ERR_INVAL, or WY_ERR_NOMEM / the underlying error.
 */
wy_error wy_host_new(const wy_host_config* config, wy_host** out);

/** Destroy a host and everything it created. NULL is fine. */
void wy_host_free(wy_host* host);

/**
 * The underlying context, for anything this facade does not cover (the full C
 * API in wyrm/*.h). Values obtained from it are owned by the host's heap.
 */
wy_context* wy_host_context(wy_host* host);

/* -------------------------------------------------------------------------
 * Running code
 * ------------------------------------------------------------------------- */

/*
 * Return codes of the calls that run code:
 *   WY_ERR_NONE          it ran
 *   WY_ERR_IMAGE         it did not compile (syntax or semantic error)
 *   WY_ERR_FAULT         it ran and faulted (an uncaught error at run time)
 *   WY_ERR_SESSION_FULL  the session's reserved space is exhausted
 * On any failure wy_host_error() has the message, and the host is exactly as
 * usable as before: a failed input changes nothing that was not already
 * stored by a fault part-way through.
 */

/**
 * Compile and run `source` as one input in the session. `source` is a whole
 * program (any number of statements, blocks and definitions). If its last
 * statement is a bare expression, that is the value stored in `*result`
 * (NULL result = do not care). The value is valid until the next call on this
 * host; copy it out (wy_host_format, wy_host_value_*) or store it in a
 * variable to keep it.
 */
wy_error wy_host_eval(wy_host* host, const char* source, wy_value* result);

/**
 * Run the script at `path` in the session: exactly as if its text were passed
 * to wy_host_eval, so its definitions and variables stay available. The
 * script's directory is added as an import root, so it can `import` sibling
 * modules. Errors are prefixed with the path.
 */
wy_error wy_host_load_file(wy_host* host, const char* path);

/** The message for the last failed call ("" if none). Valid until the next call. */
const char* wy_host_error(const wy_host* host);

/**
 * For building a REPL: true when `source` is an unfinished input that should be
 * continued with more lines (an open bracket, a block in progress, ...). A blank
 * line always ends a block, so callers submit on a blank line regardless.
 */
bool wy_host_needs_more(wy_host* host, const char* source);

/**
 * The interactive loop of `wyrm -i`, reading lines from `in` until end of
 * input or `:quit`: read, evaluate, print non-nil results, report errors, keep
 * going (commands `:quit :reset :help`; see doc-llm/repl.md). Prompts are printed
 * only when `interactive`. Returns 0 on a normal exit.
 */
int wy_host_repl(wy_host* host, FILE* in, bool interactive);

/* -------------------------------------------------------------------------
 * Variables (module-level names of the session)
 * ------------------------------------------------------------------------- */

/*
 * `name` must be a plain identifier ([A-Za-z_][A-Za-z0-9_]*). Setting a name
 * that does not exist declares it; setting one that does rebinds it, so code
 * that already refers to it sees the new value.
 *
 * Return codes: WY_ERR_NONE; WY_ERR_UNBOUND (get: no such variable);
 * WY_ERR_BAD_TYPE (a typed getter found a value of another type);
 * WY_ERR_INVAL (a bad name or argument); WY_ERR_RANGE (a buffer too small,
 * with the needed length in *out_len).
 */
wy_error wy_host_set(wy_host* host, const char* name, wy_value value);
wy_error wy_host_get(wy_host* host, const char* name, wy_value* out);
bool wy_host_has(wy_host* host, const char* name);

wy_error wy_host_set_nil(wy_host* host, const char* name);
wy_error wy_host_set_bool(wy_host* host, const char* name, bool value);
wy_error wy_host_set_int(wy_host* host, const char* name, long value);
wy_error wy_host_set_float(wy_host* host, const char* name, double value);
wy_error wy_host_set_string(wy_host* host, const char* name, const char* value);

wy_error wy_host_get_bool(wy_host* host, const char* name, bool* out);
wy_error wy_host_get_int(wy_host* host, const char* name, long* out);
/** An integer is accepted too and converted. */
wy_error wy_host_get_float(wy_host* host, const char* name, double* out);
/** Copies the text (NUL-terminated) into `buf`; *out_len is the length without the NUL. */
wy_error wy_host_get_string(wy_host* host, const char* name, char* buf, size_t cap, size_t* out_len);

/**
 * Render any value as text, the way `println` shows it (a string is its own
 * text). Copies into `buf` (NUL-terminated, truncated if `cap` is too small);
 * *out_len, if given, is the full length. WY_ERR_RANGE if truncated.
 */
wy_error wy_host_format(wy_host* host, wy_value value, char* buf, size_t cap, size_t* out_len);

/* -------------------------------------------------------------------------
 * Internal (not ABI): shared with the wyrm executable.
 * ------------------------------------------------------------------------- */

/** Run the embedded compiler's compile_source on `compile_ctx`; see main.c/host.c. */
wy_error wy_host_compile_source_bytes_(wy_context* compile_ctx, const char* path, const wy_u8* source,
    wy_uword source_len, wy_u8** out_bytes, wy_uword* out_len, const char** msg);
/** The import hook's compile function (wy_import_fs_search_path::compile); `ud` is the search path. */
wy_error wy_host_compile_on_scratch_(void* ud, wy_context* requester, const char* source_path,
    const wy_u8* source, wy_uword source_len, wy_u8** out_bytes, wy_uword* out_len, const char** msg);

WY_END_DECLS

#endif
