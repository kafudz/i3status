// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define STRING_SIZE 16

/*
 * Appends msg to the output buffer without ever writing past buf + buflen,
 * always keeping the result NUL-terminated. Returns the new end of output.
 */
static char *append_clamped(char *outwalk, char *buf, size_t buflen, const char *msg) {
    if (outwalk >= buf + buflen)
        return outwalk;
    const size_t avail = (size_t)(buf + buflen - outwalk);
    const int n = snprintf(outwalk, avail, "%s", msg);
    if (n < 0)
        return outwalk;
    size_t written = (size_t)n;
    if (written >= avail)
        written = avail - 1;
    return outwalk + written;
}

void print_load(load_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    /* Get load */

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__linux__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__) || defined(sun) || defined(__DragonFly__)
    double loadavg[3] = {0.0, 0.0, 0.0};
    const char *selected_format = ctx->format;
    bool colorful_output = false;

    /* getloadavg() returns the number of samples it retrieved (possibly fewer
     * than requested), or -1 on error. Only trust the result if we got all
     * three, otherwise the missing ones would be printed as garbage. */
    if (getloadavg(loadavg, 3) != 3)
        goto error;

    if (loadavg[0] >= ctx->max_threshold) {
        START_COLOR("color_bad");
        colorful_output = true;
        if (ctx->format_above_threshold != NULL)
            selected_format = ctx->format_above_threshold;
    }

    char string_loadavg_1[STRING_SIZE];
    char string_loadavg_5[STRING_SIZE];
    char string_loadavg_15[STRING_SIZE];

    snprintf(string_loadavg_1, STRING_SIZE, "%1.2f", loadavg[0]);
    snprintf(string_loadavg_5, STRING_SIZE, "%1.2f", loadavg[1]);
    snprintf(string_loadavg_15, STRING_SIZE, "%1.2f", loadavg[2]);

    placeholder_t placeholders[] = {
        {.name = "%1min", .value = string_loadavg_1},
        {.name = "%5min", .value = string_loadavg_5},
        {.name = "%15min", .value = string_loadavg_15}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(selected_format, &placeholders[0], num);
    if (formatted != NULL) {
        /* not OUTPUT_FORMATTED: it advances outwalk by strlen(formatted) even
         * when strncpy truncated, which leaves outwalk past the buffer */
        outwalk = append_clamped(outwalk, ctx->buf, ctx->buflen, formatted);
        free(formatted);
    }

    if (colorful_output)
        END_COLOR;

    *outwalk = '\0';
    OUTPUT_FULL_TEXT(ctx->buf);

    return;
error:
#endif
    OUTPUT_FULL_TEXT("cant read load");
    (void)fputs("i3status: Cannot read system load using getloadavg()\n", stderr);
}
