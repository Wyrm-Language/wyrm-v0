#ifndef WYRM_SYS_THREAD_COMMON_H_
#define WYRM_SYS_THREAD_COMMON_H_

// ---- Platform thread backend selection -------------------------------------
//
// Detects which threading primitive backend to use.  The result is one of:
//   WYRM_THREAD_USE_C11  — C11 <threads.h> available
//   WYRM_THREAD_USE_NONE — no OS threading; single-thread stubs only
//
// Set WYRM_THREAD_IMPL explicitly before including this header to override.

#ifndef WYRM_THREAD_IMPL

#ifdef __has_include
#if __has_include(<threads.h>)
#define WYRM_THREAD_USE_C11 1
#define WYRM_THREAD_IMPL "C11"
#endif
#endif

#if !defined(WYRM_THREAD_IMPL) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define WYRM_THREAD_USE_C11 1
#define WYRM_THREAD_IMPL "C11"
#endif

#ifndef WYRM_THREAD_IMPL
#define WYRM_THREAD_USE_NONE 1
#define WYRM_THREAD_IMPL "NONE"
#endif

#endif // WYRM_THREAD_IMPL

#endif // WYRM_SYS_THREAD_COMMON_H_
