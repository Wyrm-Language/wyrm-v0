/*
 * embed_repl: give your users a REPL in a few lines. Anything you set on the host
 * beforehand is visible to what they type.
 *
 *     $ embed_repl
 *     >>> greeting
 *     'hello from the host'
 *     >>> volume * 2
 *     22
 *
 * Input is read from standard input; prompts appear only on a terminal, so it
 * also works with a pipe.
 */
#include <wyrm/host.h>

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    wy_host* host = NULL;
    if (wy_host_new(NULL, &host) != WY_ERR_NONE) { return 1; }

    wy_host_set_string(host, "greeting", "hello from the host");
    wy_host_set_int(host, "volume", 11);

    int status = wy_host_repl(host, stdin, isatty(0) != 0);

    wy_host_free(host);
    return status;
}
