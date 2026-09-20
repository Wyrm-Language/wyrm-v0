#ifndef WYRM_HOST_INTERNAL_H_
#define WYRM_HOST_INTERNAL_H_

#include <wyrm/host.h>

#include <wyrm/context.h>
#include <wyrm/module.h>
#include <wyrm/platform/hosted/import_fs.h>
#include <wyrm/session.h>

/* The host object, shared by host.c and repl.c. Not installed. */
struct wy_host
{
    wy_machine* machine;
    wy_context* context;
    wy_import_fs_search_path search;
    char* cache_dir;            /* owned copy, absolute */
    char** dirs;                /* import roots added by wy_host_load_file */
    size_t dir_count;
    wy_io_write_fn output;
    void* output_ud;
    wy_session_config session_config;

    wy_module* compiler;        /* wyrm::tools::compile_source */
    wy_module* session_module;  /* what runs */
    wy_value fn_new, fn_compile, fn_incomplete, fn_undo;
    wy_value session;           /* the compiler's SessionContext (rooted) */
    wy_value result;            /* last eval result (rooted) */
    char error[512];
};

/* Start a fresh session (drops the previous one). Used by wy_host_new and :reset. */
wy_error wy_host_reset_session_(wy_host* host);

/* Evaluate `len` bytes of `source`; the result value lands in host->result. */
wy_error wy_host_eval_(wy_host* host, const char* source, size_t len);

#endif
