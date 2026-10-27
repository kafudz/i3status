// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

/*
 * Longest fixed string we build is a long weekday/season name plus an
 * ordinal suffix ("Prickle-Prickle" = 15, plus "th" = 17), so 32 is
 * comfortable.  The old value of 20 was already tight for "Bureaucracy"
 * plus suffix.
 */
#define STRING_SIZE 32

/* define fixed output-Strings */
static const char *season_long[5] = {
    "Chaos",
    "Discord",
    "Confusion",
    "Bureaucracy",
    "The Aftermath"};

static const char *season_short[5] = {
    "Chs",
    "Dsc",
    "Cfn",
    "Bcy",
    "Afm"};

static const char *day_long[5] = {
    "Sweetmorn",
    "Boomtime",
    "Pungenday",
    "Prickle-Prickle",
    "Setting Orange"};

static const char *day_short[5] = {
    "SM",
    "BT",
    "PD",
    "PP",
    "SO"};

static const char *holidays[10] = {
    "Mungday",
    "Mojoday",
    "Syaday",
    "Zaraday",
    "Maladay",
    "Chaoflux",
    "Discoflux",
    "Confuflux",
    "Bureflux",
    "Afflux"};

/* A helper-struct, taking the discordian date */
struct disc_time {
    int year;
    int season;
    int week_day;
    int season_day;
    int st_tibs_day;
};

/*
 * Gregorian leap-year test for the *calendar* year `Y`.
 *
 * The previous code tested `tm_year` directly.  tm_year is years since
 * 1900, which happens to preserve mod 4 and mod 100, but NOT mod 400
 * (1900 % 400 == 300), so it misclassified real years 2300, 2700, … as
 * leap and 2400, 2800, … as non-leap.  It also dropped the "!= 0" from
 * the century test.  Compute the calendar year first.
 */
static int is_gregorian_leap_year(int tm_year) {
    const int year = 1900 + tm_year;
    return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

/* Get the current date and convert it to discordian */
struct disc_time *get_ddate(struct tm *current_tm) {
    static struct disc_time dt;

    if (current_tm == NULL)
        return NULL;

    const int is_leap_year = is_gregorian_leap_year(current_tm->tm_year);

    /* tm_yday is 0-based; St. Tib's Day is the 60th day of a leap year
     * (0-based index 59). */
    int yday = current_tm->tm_yday;

    /*
     * Default all fields so that a Tib's Day result is well-defined even
     * though season_day / week_day have no meaning on that day.
     */
    dt.st_tibs_day = 0;
    dt.season_day = 0;
    dt.week_day = 0;

    if (is_leap_year && yday == 59) {
        /* On St. Tib's Day we don't have to define a date. */
        dt.st_tibs_day = 1;
    } else {
        if (is_leap_year && yday > 59)
            yday -= 1; /* skip St. Tib's Day */

        dt.season_day = yday % 73;
        dt.week_day = yday % 5;
    }

    dt.year = current_tm->tm_year + 3066;
    dt.season = yday / 73;
    return &dt;
}

/* Append the correct English ordinal suffix for 1..73. */
static void ordinal_suffix(char *dst, size_t dstsz, int ordinal) {
    const char *suffix;
    if (ordinal >= 11 && ordinal <= 13) {
        suffix = "th";
    } else {
        switch (ordinal % 10) {
            case 1: suffix = "st"; break;
            case 2: suffix = "nd"; break;
            case 3: suffix = "rd"; break;
            default: suffix = "th"; break;
        }
    }
    strncat(dst, suffix, dstsz - strlen(dst) - 1);
}

void print_ddate(ddate_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    struct tm current_tm;
    struct disc_time *dt;

    set_timezone(NULL); /* Use local time. */
    localtime_r(&ctx->t, &current_tm);
    if ((dt = get_ddate(&current_tm)) == NULL)
        return;

    /*
     * All string_* buffers are initialized to "" so that placeholders whose
     * value is not relevant today (notably %H on non-holidays and %N on
     * holidays) expand to the empty string instead of stack garbage.  The
     * original code left %H and %N uninitialized on exactly those days,
     * which corrupted the default format "%{%a, %b %d%}, %Y%N - %H".
     */
    char string_A[STRING_SIZE] = "";
    char string_a[STRING_SIZE] = "";
    char string_B[STRING_SIZE] = "";
    char string_b[STRING_SIZE] = "";
    char string_d[STRING_SIZE] = "";
    char string_e[STRING_SIZE] = "";
    char string_Y[STRING_SIZE] = "";
    char string_H[STRING_SIZE] = "";
    char string_N[STRING_SIZE] = "";
    char string_n[2] = {'\n', '\0'};
    char string_t[2] = {'\t', '\0'};

    if (dt->st_tibs_day) {
        /*
         * St. Tib's Day: "%{%a, %b %d%}" must render as "St. Tib's Day".
         * Because %{ and %} are plain substitutions in format_placeholders
         * (not conditional delimiters), leaving %a/%b/%d populated would
         * produce e.g. "St. Tib's DayPD, Cfn 1st".  Empty them out.
         */
        snprintf(string_A, sizeof(string_A), "St. Tib's Day");
        string_a[0] = '\0';
        string_B[0] = '\0';
        string_b[0] = '\0';
        string_d[0] = '\0';
        string_e[0] = '\0';
    } else {
        /* Weekday in long and abbreviation */
        snprintf(string_A, sizeof(string_A), "%s", day_long[dt->week_day]);
        snprintf(string_a, sizeof(string_a), "%s", day_short[dt->week_day]);
        /* Season in long and abbreviation */
        snprintf(string_B, sizeof(string_B), "%s", season_long[dt->season]);
        snprintf(string_b, sizeof(string_b), "%s", season_short[dt->season]);

        /* Day of the season (ordinal and cardinal).  season_day is 0-based;
         * the *displayed* ordinal is season_day + 1, and the suffix must be
         * chosen from that value.  The old code used season_day % 10 for the
         * suffix and additionally appended "th" for 11..13, yielding
         * "11thst", "12thnd", "13thrd". */
        const int ordinal = dt->season_day + 1;
        snprintf(string_d, sizeof(string_d), "%d", ordinal);
        snprintf(string_e, sizeof(string_e), "%d", ordinal);
        ordinal_suffix(string_e, sizeof(string_e), ordinal);

        /* Holidays: first five are on day 5 of each season (0-based 4),
         * the "flux" holidays are on day 50 of each season (0-based 49). */
        if (dt->season_day == 4) {
            snprintf(string_H, sizeof(string_H), "%s", holidays[dt->season]);
        } else if (dt->season_day == 49) {
            snprintf(string_H, sizeof(string_H), "%s",
                     holidays[dt->season + 5]);
        }

        /* %N is a newline that should only appear on holidays. */
        if (dt->season_day == 4 || dt->season_day == 49) {
            string_N[0] = '\n';
            string_N[1] = '\0';
        }
    }

    /* YOLD */
    snprintf(string_Y, sizeof(string_Y), "%d", dt->year);

    placeholder_t placeholders[] = {
        {.name = "%A", .value = string_A},
        {.name = "%a", .value = string_a},
        {.name = "%B", .value = string_B},
        {.name = "%b", .value = string_b},
        {.name = "%d", .value = string_d},
        {.name = "%e", .value = string_e},
        {.name = "%Y", .value = string_Y},
        {.name = "%H", .value = string_H},
        {.name = "%N", .value = string_N},
        {.name = "%n", .value = string_n},
        {.name = "%t", .value = string_t},
        /* %{ ... %} wraps the whole date; kept for format compatibility. */
        {.name = "%{", .value = (dt->st_tibs_day ? "St. Tib's Day" : "")},
        {.name = "%}", .value = (dt->st_tibs_day ? "" : "")}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(ctx->format, &placeholders[0], num);
    OUTPUT_FORMATTED;
    free(formatted);
    OUTPUT_FULL_TEXT(ctx->buf);
}