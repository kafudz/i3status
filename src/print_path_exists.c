// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <sys/stat.h>
#include "i3status.h"

/* Longest status token is "yes"/"no"; 8 leaves room for future values. */
#define STRING_SIZE 8

void print_path_exists(path_exists_ctx_t *ctx) {
    char *outwalk = ctx->buf;

    /*
     * BUG FIX: the i3bar "instance" must identify the module, i.e. the
     * section title, not the monitored path.  Every sibling module
     * (run_watch, wireless, ethernet, …) uses `title`.  Using `path` here
     * produced the wrong instance name and could break JSON output when the
     * path contained a quote or backslash.
     *
     * INSTANCE() must be called even when we bail out below, so that i3bar
     * always receives a well-formed object for this module.
     */
    INSTANCE(ctx->title != NULL ? ctx->title : "");

    /*
     * Defensive: `path` has no default in the config schema, so a missing
     * `path = "..."` line yields NULL here.  stat(NULL, …) is unspecified
     * (EFAULT on Linux, possible crash elsewhere), so treat it as a
     * non-existent path and skip the stat entirely.
     */
    struct stat st;
    const bool exists = (ctx->path != NULL && stat(ctx->path, &st) == 0);

    /*
     * If the path is missing and a format_down was configured, use that
     * template; otherwise use the normal format (which may itself reference
     * %status, which will then be "no").
     */
    const char *walk = (exists || ctx->format_down == NULL)
                           ? ctx->format
                           : ctx->format_down;

    START_COLOR(exists ? "color_good" : "color_bad");

    char string_status[STRING_SIZE];
    snprintf(string_status, sizeof(string_status), "%s",
             exists ? "yes" : "no");

    placeholder_t placeholders[] = {
        {.name = "%title", .value = ctx->title != NULL ? ctx->title : ""},
        {.name = "%status", .value = string_status}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(walk, placeholders, num);
    OUTPUT_FORMATTED;
    free(formatted);

    END_COLOR;
    OUTPUT_FULL_TEXT(ctx->buf);
}