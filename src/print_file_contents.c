// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "i3status.h"

#define STRING_SIZE 16

void print_file_contents(file_contents_ctx_t *ctx) {
    const char *walk = ctx->format;
    char *outwalk = ctx->buf;
    
    if (ctx->path == NULL) {
        START_COLOR("color_bad");
        outwalk += sprintf(outwalk, "error: path not configured");
        END_COLOR;
        OUTPUT_FULL_TEXT(ctx->buf);
        return;
    }

    char *abs_path = resolve_tilde(ctx->path);
    int fd = open(abs_path, O_RDONLY);
    free(abs_path);
    
    INSTANCE(ctx->path);

    /* Allocate buffer: max_chars + 1 for the null terminator */
    char *buf = smalloc(ctx->max_chars + 1);
    memset(buf, 0, ctx->max_chars + 1);
    
    int saved_errno = 0;
    bool is_error = false;

    if (fd == -1) {
        saved_errno = errno;
        is_error = true;
    } else {
        ssize_t n = read(fd, buf, ctx->max_chars);
        close(fd);
        
        if (n == -1) {
            saved_errno = errno;
            is_error = true;
            buf[0] = '\0';
        } else {
            buf[n] = '\0';
            
            /* Remove newline and carriage return characters cleanly */
            char *src = buf;
            char *dst = buf;
            while (*src) {
                if (*src != '\n' && *src != '\r') {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';
        }
    }

    if (is_error) {
        if (ctx->format_bad != NULL) {
            walk = ctx->format_bad;
        }
        START_COLOR("color_bad");
    } else {
        START_COLOR("color_good");
    }

    char string_errno[STRING_SIZE];
    snprintf(string_errno, sizeof(string_errno), "%d", saved_errno);
    
    const char *error_str = (saved_errno != 0) ? strerror(saved_errno) : "none";
    const char *title_str = (ctx->title != NULL) ? ctx->title : "";

    placeholder_t placeholders[] = {
        {.name = "%title",   .value = title_str},
        {.name = "%content", .value = buf},
        {.name = "%errno",   .value = string_errno},
        {.name = "%error",   .value = error_str}
    };

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(walk, placeholders, num);
    
    OUTPUT_FORMATTED;
    free(formatted);
    free(buf);
    END_COLOR;
    
    OUTPUT_FULL_TEXT(ctx->buf);
}
