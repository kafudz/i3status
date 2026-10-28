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

/*
 * Buffer for the intermediate strftime() result that feeds the %time
 * placeholder.  It must be large enough to hold the output of an arbitrary
 * user-supplied strftime format; we use the same upper bound as the output
 * buffer in main(), so a single strftime() of the full line can never
 * overflow it.
 */
#define TIME_STRING_SIZE 4096

/*
 * set_timezone() switches the process timezone to `tz`.
 *
 * Passing NULL or "" means "use the machine's local timezone" (i.e. revert
 * to the value of $TZ that was present when i3status started).  The saved
 * value is *copied* because getenv() returns a pointer into the environment
 * block, which may be reallocated by a later setenv()/unsetenv() call.
 */
static bool tz_snapshot_taken = false;
static char *saved_tz = NULL;        /* strdup()'d snapshot of $TZ, or NULL */
static const char *active_tz = NULL; /* zone currently in effect */

void set_timezone(const char *tz) {
    if (!tz_snapshot_taken) {
        /* sstrdup(NULL) == NULL, so an unset $TZ is handled correctly. */
        saved_tz = sstrdup(getenv("TZ"));
        tz_snapshot_taken = true;
    }

    if (tz == NULL || tz[0] == '\0') {
        /* User wants localtime: restore the original $TZ (which may be unset). */
        tz = saved_tz;
    }

    /*
     * Compare by content, not by pointer: cfg_getstr() returns the same
     * pointer for repeated lookups, but resolving "localtime" yields the
     * saved pointer, and NULL is a valid zone.  A string compare also
     * avoids redundant setenv()/tzset() calls when two equal strings have
     * different addresses.
     */
    const bool same = (tz == active_tz) ||
                      (tz != NULL && active_tz != NULL &&
                       strcmp(tz, active_tz) == 0);

    if (!same) {
        if (tz != NULL) {
            setenv("TZ", tz, 1);
        } else {
            unsetenv("TZ");
        }
        active_tz = tz;
    }
    tzset();
}

/*
 * Returns true if the two broken-down times denote the same UTC offset at
 * the instant they were derived from, i.e. the target zone and the local
 * zone display the same wall-clock time for ctx->t.
 *
 * When tm_gmtoff is unavailable we return false and let the caller fall
 * back to comparing the wall-clock fields.  (tm_isdst is intentionally not
 * compared: it is a hint about DST, and two zones can legitimately have the
 * same displayed time while disagreeing on the DST flag.)
 */
static bool same_utc_offset(const struct tm *local_tm, const struct tm *tm) {
#if defined(__USE_MISC) || defined(__USE_BSD) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    return local_tm->tm_gmtoff == tm->tm_gmtoff;
#else
    (void)local_tm;
    (void)tm;
    return false;
#endif
}

/*
 * Append `msg` to `buf` at `*outwalk`, clamping the write pointer so that a
 * truncated snprintf() (whose return value is the would-be length) cannot
 * advance `outwalk` past the end of the buffer.  Returns the new outwalk.
 */
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
        written = avail - 1; /* truncated; leave room for the NUL */
    return outwalk + written;
}

void print_time(time_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    struct tm local_tm, tm;

    if (ctx->title != NULL) {
        /*
         * NOTE: the INSTANCE() macro now caches its argument in a
         * `const char *` local and casts each use internally, so no cast is
         * needed (or wanted) here.  Casting at the call site breaks the
         * strlen() side of the macro under -Wpointer-sign.
         */
        INSTANCE(ctx->title);
    }

    set_timezone(NULL);
    localtime_r(&ctx->t, &local_tm);

    set_timezone(ctx->tz);
    localtime_r(&ctx->t, &tm);

    /*
     * Hide the module if the target zone shows the same wall-clock time as
     * the machine's local zone.
     */
    bool hide = false;
    if (ctx->hide_if_equals_localtime) {
        hide = same_utc_offset(&local_tm, &tm);
        if (!hide) {
            /*
             * Portable fallback: compare the wall-clock fields of the two
             * zones.  tm_sec is always equal (both derived from ctx->t),
             * and tm_isdst is a hint we intentionally ignore.
             */
            hide = (local_tm.tm_hour == tm.tm_hour &&
                    local_tm.tm_min == tm.tm_min);
        }
    }
    if (hide)
        goto out;

    /* Save the caller's locale so we can restore exactly what was set. */
    char *saved_locale = NULL;
    if (ctx->locale != NULL && ctx->locale[0] != '\0') {
        const char *cur = setlocale(LC_ALL, NULL);
        if (cur != NULL) {
            /* setlocale(NULL, ...) returns a pointer into static storage,
             * so copy it before the subsequent setlocale() call. */
            saved_locale = sstrdup(cur);
        }
        if (setlocale(LC_ALL, ctx->locale) == NULL) {
            fprintf(stderr,
                    "i3status: invalid locale \"%s\" for tztime module\n",
                    ctx->locale);
        }
    }

    char string_time[TIME_STRING_SIZE];

    if (ctx->format_time == NULL || ctx->format_time[0] == '\0') {
        /*
         * No separate time format: strftime() directly into the output
         * buffer.
         *
         * An empty format string is a valid configuration that expands to
         * nothing; strftime() returns 0 for that case just as it does on
         * overflow, so distinguish the two.
         */
        if (ctx->format[0] == '\0') {
            /* Nothing to print; leave ctx->buf empty. */
        } else {
            const size_t written =
                strftime(ctx->buf, ctx->buflen, ctx->format, &tm);
            if (written == 0) {
                outwalk = append_clamped(outwalk, ctx->buf, ctx->buflen,
                                         "i3status: time format output too large");
            } else {
                outwalk += written;
            }
        }
    } else {
        /*
         * ctx->format_time is the strftime spec that produces the %time
         * value; ctx->format is the outer template containing %time.
         */
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