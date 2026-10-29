// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
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

void print_file_contents(file_contents_ctx_t *ctx) {
    const char *walk = ctx->format;
    char *outwalk = ctx->buf;

    if (ctx->path == NULL) {
        OUTPUT_FULL_TEXT("error: path not configured");
        return;
    }
    if (ctx->max_chars <= 0) {
        /* a zero or negative size would otherwise become a huge size_t */
        OUTPUT_FULL_TEXT("error: max_characters must be greater than 0");
        return;
    }

    INSTANCE(ctx->path);

    /* one extra byte for the terminating NUL; scalloc() zeroes the buffer, so
     * it is a valid empty string if the read below fails */
    char *buf = scalloc((size_t)ctx->max_chars + 1);

    /* errno is only meaningful right after a failing call, and may be left
     * over from something unrelated otherwise. Capture it immediately and
     * use `err` (0 = success) from here on. */
    int err = 0;
    char *abs_path = resolve_tilde(ctx->path);
    const int fd = open(abs_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        err = errno;
    free(abs_path);

    if (fd >= 0) {
        ssize_t n;
        do {
            n = read(fd, buf, (size_t)ctx->max_chars);
        } while (n == -1 && errno == EINTR); /* e.g. SIGUSR1 refresh */
        if (n < 0)
            err = errno;
        else
            buf[n] = '\0';
        (void)close(fd);
    }

    if (err == 0) {
        START_COLOR("color_good");
    } else {
        walk = ctx->format_bad;
        START_COLOR("color_bad");
    }

    // remove newline chars
    char *src, *dst;
    for (src = dst = buf; *src != '\0'; src++) {
        *dst = *src;
        if (*dst != '\n') {
            dst++;
        }
    }
    *dst = '\0';

    char string_errno[STRING_SIZE];
    snprintf(string_errno, sizeof(string_errno), "%d", err);

    placeholder_t placeholders[] = {
        {.name = "%title", .value = ctx->title ? ctx->title : ""},
        {.name = "%content", .value = buf},
        {.name = "%errno", .value = string_errno},
        {.name = "%error", .value = strerror(err)}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(walk, &placeholders[0], num);
    if (formatted != NULL) {
        /* not OUTPUT_FORMATTED: it advances outwalk by strlen(formatted) even
         * when strncpy truncated, which leaves outwalk past the buffer */
        outwalk = append_clamped(outwalk, ctx->buf, ctx->buflen, formatted);
        free(formatted);
    }
    free(buf);

    END_COLOR;
    OUTPUT_FULL_TEXT(ctx->buf);
}
