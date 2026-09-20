#ifndef WYRM_REPL_H_
#define WYRM_REPL_H_

#include <wyrm.h>

/*
 * The interactive read-eval-print loop (`wyrm -i`, doc/repl-plan.md).
 *
 * `context` must be fully prepared as for running a script: fiber attached,
 * builtins, the std::io natives, std::expand, and the import hook with the
 * builtin table. The loop compiles each complete input with the embedded
 * compiler in this same context, extends one session module with it, runs it,
 * and prints a non-nil result. It reads standard input; prompts are printed
 * only when `interactive` (a terminal), so a piped transcript is just output.
 *
 * Returns the process exit status (0 on :quit or end of input).
 */
int wyrm_repl(wy_context* context, bool interactive);

#endif
