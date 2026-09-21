#define _POSIX_C_SOURCE 200809L

#include <wyrm/host.h>

#include "host_internal.h"

#include <wyrm/builtins.h>
#include <wyrm/string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The interactive loop (`wyrm -i`, wy_host_repl). See doc-llm/repl.md for what the
 * user sees; doc-llm/history/repl/repl-plan.md for the design.
 */

static void emit_(wy_host* host, const char* text, size_t len)
{
    host->context->io.write(host->context, text, (wy_uword) len, host->context->io.ud);
}

/* Strings in single quotes (wy strings have no escape for the double quote);
 * everything else is rendered exactly as the `println` builtin renders it. */
static void show_value_(wy_host* host, wy_value value)
{
    if (value.type == WY_TYPE_TAG_STR) {
        wy_string* s = (wy_string*) value.data.gc_object;
        emit_(host, "'", 1);
        emit_(host, s->str, s->len);
        emit_(host, "'\n", 2);
        return;
    }
    wy_value ignored[1] = { wy_value_nil() };
    if (wy_builtin_println_body_f(host->context, &value, 1, ignored, 0) != WY_ERR_NONE) {
        fprintf(stderr, "wyrm: cannot display the result\n");
    }
}

/* Compile, run and show one complete input. Never ends the session. */
static void run_input_(wy_host* host, const char* src, size_t len)
{
    wy_error err = wy_host_eval_(host, src, len);
    fflush(stdout);
    switch (err) {
    case WY_ERR_NONE:
        if (host->result.type != WY_TYPE_TAG_NIL) { show_value_(host, host->result); }
        break;
    case WY_ERR_IMAGE:
        fprintf(stderr, "error: %s\n", host->error);
        break;
    case WY_ERR_FAULT:
        fprintf(stderr, "fault: %s\n", host->error);
        break;
    case WY_ERR_SESSION_FULL:
        fprintf(stderr, "error: session limit reached; :reset to start over\n");
        break;
    default:
        fprintf(stderr, "wyrm: %s\n", host->error[0] ? host->error : "internal error");
        break;
    }
}

static bool is_blank_(const char* line)
{
    for (; *line; line++) {
        if (*line != ' ' && *line != '\t' && *line != '\n' && *line != '\r') { return false; }
    }
    return true;
}

int wy_host_repl(wy_host* host, FILE* in, bool interactive)
{
    if (host == WY_NULL || in == WY_NULL) { return 1; }

    char* line = WY_NULL;
    size_t line_cap = 0;
    char* buffer = WY_NULL;
    size_t buffer_len = 0, buffer_cap = 0;

    for (;;) {
        if (interactive) {
            fputs(buffer_len == 0 ? ">>> " : "... ", stdout);
            fflush(stdout);
        }
        ssize_t got = getline(&line, &line_cap, in);
        if (got < 0) {
            if (interactive) { fputc('\n', stdout); }
            /* End of input with a half-typed input pending (an unclosed bracket):
             * submit it so the user sees the error rather than losing it silently. */
            if (buffer_len > 0 && !interactive) { run_input_(host, buffer, buffer_len); }
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
                    if (wy_host_reset_session_(host) != WY_ERR_NONE) { break; }
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

        /* Keep reading while the input is unfinished (open bracket, block). */
        if (!blank && wy_host_needs_more(host, buffer)) { continue; }
        run_input_(host, buffer, buffer_len);
        buffer_len = 0;
    }

    free(line);
    free(buffer);
    return 0;
}
