// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define STRING_SIZE 32
#define DEFAULT_BACKLIGHT_GLOB "/sys/class/backlight/*"

/*
 * Returns the parent directory of path, allocated on the heap. A path without
 * a slash is relative to the current directory.
 *
 */
static char *parent_directory(const char *path) {
    char *copy = sstrdup(path);
    size_t len = strlen(copy);

    while (len > 1 && copy[len - 1] == '/')
        copy[--len] = '\0';

    char *slash = strrchr(copy, '/');
    if (slash == NULL) {
        free(copy);
        return sstrdup(".");
    }

    if (slash == copy)
        slash[1] = '\0';
    else
        *slash = '\0';

    return copy;
}

/*
 * Resolves the configured path to a backlight device directory. The path may
 * be a device directory, a glob matching device directories, or the brightness
 * file itself (in which case its parent directory is used).
 *
 */
static char *resolve_backlight_directory(const char *configured) {
    const char *config = (configured != NULL && *configured != '\0')
                             ? configured
                             : DEFAULT_BACKLIGHT_GLOB;
    char *pattern = resolve_tilde(config);
    if (pattern == NULL)
        return NULL;

    struct stat st;
    if (stat(pattern, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return pattern;

        char *parent = parent_directory(pattern);
        free(pattern);
        return parent;
    }

    glob_t globbuf;
    memset(&globbuf, 0, sizeof(globbuf));
    int glob_result = glob(pattern, GLOB_TILDE, NULL, &globbuf);
    free(pattern);

    char *directory = NULL;
    char *fallback = NULL;
    if (glob_result == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; i++) {
            if (stat(globbuf.gl_pathv[i], &st) != 0)
                continue;
            if (S_ISDIR(st.st_mode)) {
                directory = sstrdup(globbuf.gl_pathv[i]);
                break;
            }
            if (fallback == NULL)
                fallback = parent_directory(globbuf.gl_pathv[i]);
        }
    }
    globfree(&globbuf);

    return (directory != NULL) ? directory : fallback;
}

/*
 * Reads a non-negative integer from a file. Trailing whitespace (such as the
 * newline sysfs appends) is accepted, anything else is rejected.
 *
 */
static bool read_unsigned(const char *path, unsigned long *value) {
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return false;

    char buffer[64];
    if (fgets(buffer, sizeof(buffer), file) == NULL) {
        fclose(file);
        return false;
    }
    fclose(file);

    const char *walk = buffer;
    while (isspace((unsigned char)*walk))
        walk++;
    if (*walk == '-' || !isdigit((unsigned char)*walk))
        return false;

    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(buffer, &end, 10);
    if (end == buffer || errno == ERANGE)
        return false;
    while (isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return false;

    *value = parsed;
    return true;
}

static char *join_path(const char *directory, const char *name) {
    char *path = NULL;
    if (asprintf(&path, "%s/%s", directory, name) == -1)
        die("asprintf() failed\n");
    return path;
}

void print_brightness(brightness_ctx_t *ctx) {
    const char *selected_format = (ctx->format != NULL) ? ctx->format : "";
    char *outwalk = ctx->buf;

    char *directory = resolve_backlight_directory(ctx->path);
    if (directory == NULL) {
        OUTPUT_FULL_TEXT(ctx->format_down);
        return;
    }

    char *brightness_path = join_path(directory, "brightness");
    char *actual_path = join_path(directory, "actual_brightness");
    char *max_path = join_path(directory, "max_brightness");

    unsigned long brightness = 0;
    unsigned long maximum = 0;
    bool ok = read_unsigned(brightness_path, &brightness);
    if (!ok)
        ok = read_unsigned(actual_path, &brightness);

    if (!ok || !read_unsigned(max_path, &maximum) || maximum == 0) {
        free(brightness_path);
        free(actual_path);
        free(max_path);
        free(directory);
        OUTPUT_FULL_TEXT(ctx->format_down);
        return;
    }

    if (brightness > maximum)
        brightness = maximum;
    const int percentage = (int)((100.0 * (double)brightness / (double)maximum) + 0.5);

    const char *device = strrchr(directory, '/');
    device = (device != NULL) ? device + 1 : directory;

    char string_percentage[STRING_SIZE];
    char string_brightness[STRING_SIZE];
    char string_max_brightness[STRING_SIZE];
    snprintf(string_percentage, sizeof(string_percentage), "%d%s", percentage, pct_mark);
    snprintf(string_brightness, sizeof(string_brightness), "%lu", brightness);
    snprintf(string_max_brightness, sizeof(string_max_brightness), "%lu", maximum);

    INSTANCE(device);

    placeholder_t placeholders[] = {
        {.name = "%percentage", .value = string_percentage},
        {.name = "%brightness", .value = string_brightness},
        {.name = "%max_brightness", .value = string_max_brightness},
        {.name = "%device", .value = device},
    };
    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(selected_format, &placeholders[0], num);
    OUTPUT_FORMATTED;
    free(formatted);

    free(brightness_path);
    free(actual_path);
    free(max_path);
    free(directory);

    OUTPUT_FULL_TEXT(ctx->buf);
}
