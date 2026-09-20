/*
 * embed_custom_repl: build your own read-eval-print loop from the pieces
 * (wy_host_needs_more, wy_host_eval, wy_host_format, wy_host_error), when the
 * ready-made wy_host_repl is not what you want: a different prompt, your own
 * commands, per-input variables, your own error style.
 *
 * Lines are collected until the input is finished (an open bracket or block needs
 * more; a blank line ends a block), then evaluated. The variable `turn` is set
 * from C before every evaluation.
 */
#include <wyrm/host.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    wy_host* host = NULL;
    if (wy_host_new(NULL, &host) != WY_ERR_NONE) { return 1; }

    char line[512];
    char input[8192] = "";
    long turn = 0;

    for (;;) {
        printf(input[0] ? "  .. " : "wy> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) { break; }

        int blank = strspn(line, " \t\r\n") == strlen(line);
        if (input[0] == '\0') {
            if (blank) { continue; }
            if (strcmp(line, "exit\n") == 0) { break; }       /* our own command */
        }
        if (strlen(input) + strlen(line) >= sizeof(input)) { input[0] = '\0'; continue; }
        strcat(input, line);
        if (!blank && wy_host_needs_more(host, input)) { continue; }

        wy_host_set_int(host, "turn", ++turn);                 /* visible to this input's code */
        wy_value value;
        wy_error err = wy_host_eval(host, input, &value);
        if (err == WY_ERR_NONE) {
            char text[256];
            if (wy_host_format(host, value, text, sizeof(text), NULL) != WY_ERR_NONE) { text[0] = '\0'; }
            if (text[0] != '\0' && strcmp(text, "nil") != 0) { printf("=> %s\n", text); }
        } else {
            printf("%s: %s\n", err == WY_ERR_FAULT ? "fault" : "error", wy_host_error(host));
        }
        input[0] = '\0';
    }

    printf("\nbye after %ld inputs\n", turn);
    wy_host_free(host);
    return 0;
}
