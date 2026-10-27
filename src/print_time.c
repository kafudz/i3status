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
 * user-supplied strftime format (which can easily exceed 50 bytes, e.g.
 * "%A, %d %B %Y %H:%M:%S %Z (%z)").  We use the same upper bound as the
 * output buffer in main(), so a single strftime() of the full line can never
 * overflow it.
 */
#define TIME_STRING_SIZE 4096

/*
 * set_timezone() switches the process timezone to `tz`.
 *
 * Passing NULL or "" means "use the machine's local timezone" (i.e. revert
 * to the value of $TZ that was present when i3status started).  A NULL
 * snapshot is remembered separately so that "the user never set TZ" is
 * distinct from "we have not switched zones yet".
 */
static bool tz_snapshot_taken = false;
static const char *saved_tz = NULL; /* value of $TZ at first call, or NULL */
static const char *active_tz = NULL; /* zone currently in effect */

void set_timezone(const char *tz) {
    if (!tz_snapshot_taken) {
        saved_tz = getenv("TZ");
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
                      (tz != NULL && active_tz != NULL && strcmp(tz, active_tz) == 0);

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
 */
static bool same_utc_offset(const struct tm *local_tm, const struct tm *tm) {
#if defined(__USE_MISC) || defined(__USE_BSD) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    return local_tm->tm_gmtoff == tm->tm_gmtoff;
#else
    /*
     * Portable fallback: recompute each zone's offset from its broken-down
     * time.  mktime() interprets a struct tm as local time, so
     * mktime(&tm) - ctx->t yields the offset between the target zone and
     * the machine's local zone; the same subtraction for local_tm is 0 by
     * construction.  We therefore only need to check that the target zone
     * actually differs from local time.
     *
     * (Because print_time() has already selected `tm`'s zone via
     * set_timezone(), calling mktime() here would misinterpret `tm` as
     * local time; the comparison below is done in the caller instead.)
     */
    return false; /* handled by the caller's strftime fallback */
#endif
}

void print_time(time_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    struct tm local_tm, tm;

    if (ctx->title != NULL)
        INSTANCE(ctx->title);

    set_timezone(NULL);
    localtime_r(&ctx->t, &local_tm);

    set_timezone(ctx->tz);
    localtime_r(&ctx->t, &tm);

    /*
     * Determine whether to hide this module when the target zone shows the
     * same wall-clock time as the machine's local zone.
     *
     * The previous implementation compared mktime(&local_tm) with ctx->t,
     * which is ~always 0 (local_tm was produced by localtime_r(ctx->t)), so
     * the module was hidden unconditionally.  Instead, compare the UTC
     * offsets of the two zones at this instant.  When tm_gmtoff is
     * unavailable we fall back to comparing the formatted wall-clock
     * strings below.
     */
    bool hide = false;
    if (ctx->hide_if_equals_localtime) {
        hide = same_utc_offset(&local_tm, &tm);
        if (!hide) {
            /* Portable fallback: compare hour/min/sec/DST of the two zones. */
            hide = (local_tm.tm_hour == tm.tm_hour &&
                    local_tm.tm_min == tm.tm_min &&
                    local_tm.tm_sec == tm.tm_sec &&
                    local_tm.tm_isdst == tm.tm_isdst);
        }
    }
    if (hide)
        goto out;

    /* Save the caller's locale so we can restore exactly what was set. */
    const char *saved_locale = NULL;
    if (ctx->locale != NULL && ctx->locale[0] != '\0') {
        saved_locale = setlocale(LC_ALL, NULL);
        if (saved_locale != NULL) {
            /* setlocale(NULL, ...) may return a pointer into static storage,
             * so copy it before the subsequent setlocale() call. */
            saved_locale = strdup(saved_locale);
        }
        if (setlocale(LC_ALL, ctx->locale) == NULL) {
            fprintf(stderr, "i3status: invalid locale \"%s\" for tztime module\n",
                    ctx->locale);
        }
    }

    char string_time[TIME_STRING_SIZE];

    if (ctx->format_time == NULL || ctx->format_time[0] == '\0') {
        /*
         * No separate time format: strftime() directly into the output
         * buffer.  Detect failure (returns 0 when the buffer is too small)
         * and emit a diagnostic rather than a truncated/garbage line.
         */
        const size_t written = strftime(ctx->buf, ctx->buflen, ctx->format, &tm);
        if (written == 0) {
            outwalk += snprintf(ctx->buf, ctx->buflen,
                                "i3status: time format output too large");
        } else {
            outwalk += written;
        }
    } else {
        /*
         * ctx->format_time is the strftime spec that produces the %time
         * value; ctx->format is the outer template containing %time.
         * The old code mistakenly passed ctx->format_time as the
         * placeholder template, so the raw strftime spec was printed.
         */
        const size_t written =
            strftime(string_time, sizeof(string_time), ctx->format_time, &tm);
        if (written == 0) {
            /* Format produced more than sizeof(string_time) bytes. */
            outwalk += snprintf(ctx->buf, ctx->buflen,
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
        free((char *)saved_locale);
    }

out:
    *outwalk = '\0';
    OUTPUT_FULL_TEXT(ctx->buf);
}