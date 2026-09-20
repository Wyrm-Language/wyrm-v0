/*
 * embed_basic: the smallest useful embedding. Create an interpreter, give it
 * variables, run code that uses them, read results back.
 *
 * Build: it is part of the meson build (examples/meson.build); run it as
 *     ./buildDir/examples/embed_basic
 */
#include <wyrm/host.h>

#include <stdio.h>

int main(void)
{
    wy_host* host = NULL;
    if (wy_host_new(NULL, &host) != WY_ERR_NONE) {          /* NULL = default configuration */
        fprintf(stderr, "cannot start wyrm\n");
        return 1;
    }

    /* 1. Set variables the code can use. */
    wy_host_set_int(host, "width", 40);
    wy_host_set_int(host, "height", 25);
    wy_host_set_string(host, "unit", "cm");

    /* 2. Run code. Everything shares one namespace and persists between calls. */
    wy_host_eval(host, "area := width * height", NULL);
    wy_host_eval(host, "fn describe():\n    return str(area) + \" square \" + unit\n", NULL);

    /* 3. Read variables back... */
    long area = 0;
    wy_host_get_int(host, "area", &area);
    printf("area = %ld\n", area);

    /* 4. ...or evaluate an expression and turn its value into text. */
    wy_value value;
    char text[128];
    if (wy_host_eval(host, "describe()", &value) == WY_ERR_NONE) {
        wy_host_format(host, value, text, sizeof(text), NULL);
        printf("describe() = %s\n", text);
    }

    /* 5. Change a variable from C; code that already uses it sees the new value. */
    wy_host_set_int(host, "width", 10);
    wy_host_eval(host, "area = width * height", NULL);       /* `=` rebinds, `:=` would shadow */
    wy_host_get_int(host, "area", &area);
    printf("area is now %ld\n", area);

    /* 6. Failures come back as a code and a message; the host stays usable. */
    if (wy_host_eval(host, "1 +* 2", NULL) != WY_ERR_NONE) {
        printf("compile error: %s\n", wy_host_error(host));
    }
    if (wy_host_eval(host, "no_such_function()", NULL) == WY_ERR_FAULT) {
        printf("run-time fault: %s\n", wy_host_error(host));
    }
    if (wy_host_eval(host, "describe()", &value) == WY_ERR_NONE) {
        wy_host_format(host, value, text, sizeof(text), NULL);
        printf("still works: %s\n", text);
    }

    wy_host_free(host);
    return 0;
}
