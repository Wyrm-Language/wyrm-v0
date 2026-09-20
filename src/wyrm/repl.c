#define _POSIX_C_SOURCE 200809L

#include "repl.h"

#include <wyrm/builtins.h>
#include <wyrm/error.h>
#include <wyrm/fiber.h>
#include <wyrm/image_loader.h>
#include <wyrm/link.h>
#include <wyrm/list.h>
#include <wyrm/module.h>
#include <wyrm/session.h>
#include <wyrm/string.h>
#include <wyrm/bytes.h>
#include <wyrm/vm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct repl_
{
    wy_context* context;
    wy_module* host;       /* the session module: what runs */
    wy_module* compiler;   /* wyrm::tools::compile_source */
    wy_value fn_new, fn_compile, fn_incomplete, fn_undo;
    wy_value session;      /* the compiler's SessionContext (rooted) */
    wy_value result;       /* scratch (rooted): the last call's result */
} repl_;

static bool export_fn_(repl_* r, const char* name, wy_value* out)
{
    wy_symbol sym = WY_NULL;
    if (wy_context_intern(r->context, name, strlen(name), &sym) != WY_ERR_NONE) { return false; }
    wy_uword slot = wy_slot_dict_get(&r->compiler->exports, sym);
    if (slot == WY_SLOT_INVALID) { return false; }
    *out = r->compiler->globals[slot];
    return out->type == WY_TYPE_TAG_FUNCTION;
}

static const char* fault_text_(wy_context* context)
{
    wy_value fault = context->current_fiber->fault;
    if (wy_value_is_error(fault) && fault.data.gc_object != WY_NULL) {
        wy_error_obj* e = (wy_error_obj*) fault.data.gc_object;
        if (e->what != WY_NULL) { return e->what->str; }
    }
    return "fault";
}

static const char* error_value_text_(wy_value v)
{
    wy_error_obj* e = (wy_error_obj*) v.data.gc_object;
    return (e != WY_NULL && e->what != WY_NULL) ? e->what->str : "error";
}

static bool call_(repl_* r, wy_value fn, const wy_value* args, wy_uword argc, wy_value* out)
{
    wy_error err = wy_vm_call_sync(r->context, fn, args, argc, out, 1);
    if (err != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: internal error in the session compiler: %s\n", fault_text_(r->context));
        r->context->current_fiber->fault = wy_value_nil();
        return false;
    }
    return true;
}

static bool str_value_(repl_* r, const char* text, wy_uword len, wy_value* out)
{
    wy_string* s = WY_NULL;
    if (wy_string_new(r->context, text, len, &s) != WY_ERR_NONE) { return false; }
    *out = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) s);
    return true;
}

/* WYRM_SESSION_CODE_WORDS overrides the code reservation (in 32-bit words; the
 * default is 4 Mi words = 16 MiB). Mostly for tests and constrained hosts. */
static void configure_(wy_session_config* config)
{
    wy_session_config_default(config);
    const char* text = getenv("WYRM_SESSION_CODE_WORDS");
    if (text != WY_NULL) {
        char* end = WY_NULL;
        unsigned long words = strtoul(text, &end, 10);
        if (end != text && *end == '\0' && words > 0) { config->capacity[WY_SESSION_CODE] = (wy_uword) words; }
    }
}

/* Start a fresh session, dropping the previous one: its module stops being a
 * root, so the next collection frees its reservation. */
static bool new_session_(repl_* r)
{
    if (r->host != WY_NULL) {
        (void) wy_context_module_unregister(r->context, r->host);
        r->host = WY_NULL;
    }
    wy_session_config config;
    configure_(&config);
    if (wy_module_session_new(r->context, &config, &r->host) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: cannot reserve the session\n");
        return false;
    }
    return call_(r, r->fn_new, WY_NULL, 0, &r->session);
}

/* Strings in single quotes (wy strings have no escape for the double quote);
 * everything else is rendered exactly as the `println` builtin renders it. */
static void show_value_(repl_* r, wy_value value)
{
    if (value.type == WY_TYPE_TAG_STR) {
        wy_string* s = (wy_string*) value.data.gc_object;
        fputc('\'', stdout);
        fwrite(s->str, 1, s->len, stdout);
        fputs("'\n", stdout);
        return;
    }
    wy_value ignored[1] = { wy_value_nil() };
    if (wy_builtin_println_body_f(r->context, &value, 1, ignored, 0) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: cannot display the result\n");
    }
}

/* Compile, extend, run and show one complete input. Never ends the session. */
static void run_input_(repl_* r, const char* src, size_t len)
{
    wy_value args[2];
    args[0] = r->session;
    if (!str_value_(r, src, len, &args[1])) { fprintf(stderr, "wyrm: out of memory\n"); return; }
    if (!call_(r, r->fn_compile, args, 2, &r->result)) { return; }

    if (r->result.type == WY_TYPE_TAG_ERROR) {
        fflush(stdout);
        fprintf(stderr, "error: %s\n", error_value_text_(r->result));
        return;
    }
    if (r->result.type != WY_TYPE_TAG_BYTES) {
        fprintf(stderr, "wyrm: internal error: the compiler answered no image\n");
        return;
    }
    wy_bytes* blob = (wy_bytes*) r->result.data.gc_object;
    wy_module_image image;
    wy_error err = wy_image_from_bytes(blob->data, blob->len, &image);
    wy_uword init = 0;
    if (err == WY_ERR_NONE) { err = wy_module_extend(r->context, r->host, &image, &init); }
    if (err != WY_ERR_NONE) {
        if (err == WY_ERR_SESSION_FULL) {
            fprintf(stderr, "error: session limit reached; :reset to start over\n");
        } else {
            fprintf(stderr, "wyrm: internal error: cannot load the input (%d)\n", (int) err);
        }
        /* The compiler already counted this input; take it back so the two
         * sides stay in step and a smaller input can still succeed. */
        wy_value undone = wy_value_nil();
        (void) call_(r, r->fn_undo, &r->session, 1, &undone);
        return;
    }

    wy_value value = wy_value_nil();
    err = wy_module_run_function(r->context, r->host, init, &value);
    fflush(stdout);
    if (err != WY_ERR_NONE) {
        fprintf(stderr, "fault: %s\n", fault_text_(r->context));
        r->context->current_fiber->fault = wy_value_nil();
        return;
    }
    if (value.type != WY_TYPE_TAG_NIL) { show_value_(r, value); }
}

static bool is_blank_(const char* line)
{
    for (; *line; line++) {
        if (*line != ' ' && *line != '\t' && *line != '\n' && *line != '\r') { return false; }
    }
    return true;
}

int wyrm_repl(wy_context* context, bool interactive)
{
    repl_ r;
    memset(&r, 0, sizeof(r));
    r.context = context;
    r.session = wy_value_nil();
    r.result = wy_value_nil();

    wy_string* path = WY_NULL;
    if (wy_string_strdup(context, "wyrm::tools::compile_source", &path) != WY_ERR_NONE
        || wy_link_import(context, path, &r.compiler) != WY_ERR_NONE
        || wy_module_run_init(context, r.compiler) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: cannot load the embedded compiler\n");
        return 1;
    }
    if (!export_fn_(&r, "session_new", &r.fn_new) || !export_fn_(&r, "session_compile", &r.fn_compile)
        || !export_fn_(&r, "session_incomplete", &r.fn_incomplete)
        || !export_fn_(&r, "session_undo", &r.fn_undo)) {
        fprintf(stderr, "wyrm: the embedded compiler has no session entry points\n");
        return 1;
    }
    if (wy_context_root_push_f(context, &r.session) != WY_ERR_NONE
        || wy_context_root_push_f(context, &r.result) != WY_ERR_NONE) {
        return 1;
    }
    if (!new_session_(&r)) { return 1; }

    char* line = WY_NULL;
    size_t line_cap = 0;
    char* buffer = WY_NULL;
    size_t buffer_len = 0, buffer_cap = 0;

    for (;;) {
        if (interactive) {
            fputs(buffer_len == 0 ? ">>> " : "... ", stdout);
            fflush(stdout);
        }
        ssize_t got = getline(&line, &line_cap, stdin);
        if (got < 0) {
            if (interactive) { fputc('\n', stdout); }
            /* End of input with a half-typed input pending (an unclosed bracket):
             * submit it so the user sees the error rather than losing it silently. */
            if (buffer_len > 0 && !interactive) { run_input_(&r, buffer, buffer_len); }
            break;
        }
        bool blank = is_blank_(line);

        if (buffer_len == 0) {
            if (blank) { continue; }
            if (line[0] == ':') {
                size_t n = strlen(line);
                while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) { line[--n] = '\0'; }
                if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) { break; }
                if (strcmp(line, ":reset") == 0) {
                    if (!new_session_(&r)) { break; }
                    if (interactive) { puts("session reset"); }
                } else if (strcmp(line, ":help") == 0) {
                    puts(":quit  leave    :reset  start a fresh session    blank line ends a block");
                } else {
                    fprintf(stderr, "unknown command %s (:help)\n", line);
                }
                continue;
            }
        }

        size_t need = buffer_len + (size_t) got + 1;
        if (need > buffer_cap) {
            buffer_cap = need * 2;
            char* grown = (char*) realloc(buffer, buffer_cap);
            if (grown == WY_NULL) { fprintf(stderr, "wyrm: out of memory\n"); break; }
            buffer = grown;
        }
        memcpy(buffer + buffer_len, line, (size_t) got);
        buffer_len += (size_t) got;
        buffer[buffer_len] = '\0';

        if (!blank) {
            /* Keep reading while the input is unfinished (open bracket, block). */
            wy_value arg, verdict = wy_value_nil();
            if (!str_value_(&r, buffer, buffer_len, &arg)) { fprintf(stderr, "wyrm: out of memory\n"); break; }
            if (call_(&r, r.fn_incomplete, &arg, 1, &verdict) && verdict.type == WY_TYPE_TAG_BOOL
                && verdict.data.flag) {
                continue;
            }
        }
        run_input_(&r, buffer, buffer_len);
        buffer_len = 0;
    }

    free(line);
    free(buffer);
    wy_context_root_pop_f(context);
    wy_context_root_pop_f(context);
    return 0;
}
