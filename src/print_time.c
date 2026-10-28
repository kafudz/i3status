// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define TIME_STRING_SIZE 4096

static bool tz_snapshot_taken = false;
static char *saved_tz = NULL;
static const char *active_tz = NULL;

void set_timezone(const char *tz) {
    if (!tz_snapshot_taken) {
        saved_tz = sstrdup(getenv("TZ"));
        tz_snapshot_taken = true;
    }

    if (tz == NULL || tz[0] == '\0')
        tz = saved_tz;

    const bool same = (tz == active_tz) ||
                      (tz != NULL && active_tz != NULL &&
                       strcmp(tz, active_tz) == 0);

    if (!same) {
        if (tz != NULL)
            setenv("TZ", tz, 1);
        else
            unsetenv("TZ");
        active_tz = tz;
    }
    tzset();
}

static bool same_utc_offset(const struct tm *local_tm, const struct tm *tm) {
#if defined(__USE_MISC) || defined(__USE_BSD) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    return local_tm->tm_gmtoff == tm->tm_gmtoff;
#else
    (void)local_tm;
    (void)tm;
    return false;
#endif
}

static char *append_clamped(char *outwalk, char *buf, size_t buflen,
                            const char *msg) {
    if (outwalk >= buf + buflen)
        return outwalk;
    size_t avail = (size_t)(buf + buflen - outwalk);
    int n = snprintf(outwalk, avail, "%s", msg);
    if (n < 0)
        return outwalk;
    size_t written = (size_t)n;
    if (written >= avail)
        written = avail - 1;
    return outwalk + written;
}

void print_time(time_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    struct tm local_tm, tm;

    if (ctx->title != NULL)
        INSTANCE(ctx->title);   /* no cast: INSTANCE caches + casts per-use */

    set_timezone(NULL);
    localtime_r(&ctx->t, &local_tm);

    set_timezone(ctx->tz);
    localtime_r(&ctx->t, &tm);

    bool hide = false;
    if (ctx->hide_if_equals_localtime) {
        hide = same_utc_offset(&local_tm, &tm);
        if (!hide)
            hide = (local_tm.tm_hour == tm.tm_hour &&
                    local_tm.tm_min == tm.tm_min);
    }
    if (hide)
        goto out;

    char *saved_locale = NULL;
    if (ctx->locale != NULL && ctx->locale[0] != '\0') {
        const char *cur = setlocale(LC_ALL, NULL);
        if (cur != NULL)
            saved_locale = sstrdup(cur);
        if (setlocale(LC_ALL, ctx->locale) == NULL)
            fprintf(stderr,
                    "i3status: invalid locale \"%s\" for tztime module\n",
                    ctx->locale);
    }

    char string_time[TIME_STRING_SIZE];

    if (ctx->format_time == NULL || ctx->format_time[0] == '\0') {
        if (ctx->format[0] == '\0') {
            /* nothing to print */
        } else {
            const size_t written =
                strftime(ctx->buf, ctx->buflen, ctx->format, &tm);
            if (written == 0)
                outwalk = append_clamped(outwalk, ctx->buf, ctx->buflen,
                                         "i3status: time format output too large");
            else
                outwalk += written;
        }
    } else {
        const size_t written =
            strftime(string_time, sizeof(string_time), ctx->format_time, &tm);
        if (written == 0 && ctx->format_time[0] != '\0') {
            outwalk = append_clamped(outwalk, ctx->buf, ctx->buflen,
                                     "i3status: time format output too large");
        } else {
            placeholder_t placeholders[] = {
                {.name = "%time", .value = string_time}};
            const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
            char *formatted =
                format_placeholders(ctx->format, placeholders, num);
            OUTPUT_FORMATTED;
            free(formatted);
        }
    }

    if (saved_locale != NULL) {
        setlocale(LC_ALL, saved_locale);
        free(saved_locale);
    }

out:
    *outwalk = '\0';
    OUTPUT_FULL_TEXT(ctx->buf);
}