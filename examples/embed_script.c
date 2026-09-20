/*
 * embed_script: load a script, passing it variables and reading results back.
 *
 *     embed_script [path/to/script.wy]
 *
 * The default script is examples/scripts/report.wy (its directory is set at
 * build time). The script `import`s a sibling module: wy_host_load_file makes
 * the script's own directory an import root.
 */
#include <wyrm/host.h>

#include <stdio.h>

#ifndef EXAMPLE_SCRIPT
#define EXAMPLE_SCRIPT "examples/scripts/report.wy"
#endif

int main(int argc, char** argv)
{
    const char* script = argc > 1 ? argv[1] : EXAMPLE_SCRIPT;

    wy_host* host = NULL;
    if (wy_host_new(NULL, &host) != WY_ERR_NONE) { return 1; }

    /* Inputs for the script: plain variables, set before loading. */
    wy_host_set_int(host, "limit", 6);
    wy_host_set_int(host, "factor", 3);

    if (wy_host_load_file(host, script) != WY_ERR_NONE) {
        fprintf(stderr, "%s\n", wy_host_error(host));         /* prefixed with the path */
        wy_host_free(host);
        return 1;
    }

    /* Outputs: read variables the script defined... */
    char summary[128];
    long above = 0;
    wy_host_get_string(host, "summary", summary, sizeof(summary), NULL);
    wy_host_get_int(host, "above", &above);
    printf("%s\n", summary);
    printf("above = %ld\n", above);

    /* ...and call functions it defined, through eval. */
    wy_value value;
    if (wy_host_eval(host, "scale(14)", &value) == WY_ERR_NONE) {
        char text[64];
        wy_host_format(host, value, text, sizeof(text), NULL);
        printf("scale(14) = %s\n", text);
    }

    wy_host_free(host);
    return 0;
}
