// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>

#include "i3status.h"

/*
 * Returns the correct color escape for dzen (^fg(color)), xmobar (<fc=color>),
 * lemonbar (%{Fcolor}), or the terminal, or an empty string when colors are
 * disabled or the output format has no color syntax (i3bar, none).
 *
 * NOTE: the return value points into a small, *rotating* static buffer
 * pool.  Up to COLOR_RING_SIZE calls remain valid simultaneously, which is
 * enough for the common printf("%s%s%s", color(a), sep, color(b)) idiom.
 * Do not store the returned pointer across loop iterations.
 */
#define COLOR_RING_SIZE 4
#define COLOR_BUF_SIZE 32

char *color(const char *colorstr) {
    static char colorbuf_ring[COLOR_RING_SIZE][COLOR_BUF_SIZE];
    static unsigned int colorbuf_idx = 0;

    char *colorbuf = colorbuf_ring[colorbuf_idx++ % COLOR_RING_SIZE];
    /* Always start empty: avoids leaking stale output from a previous call
     * when the format has no color syntax (i3bar, none). */
    colorbuf[0] = '\0';

    if (!cfg_getbool(cfg_general, "colors"))
        return colorbuf;

    const char *value = cfg_getstr(cfg_general, colorstr);

    if (output_format == O_DZEN2)
        (void)snprintf(colorbuf, COLOR_BUF_SIZE, "^fg(%s)", value);
    else if (output_format == O_XMOBAR)
        (void)snprintf(colorbuf, COLOR_BUF_SIZE, "<fc=%s>", value);
    else if (output_format == O_LEMONBAR)
        (void)snprintf(colorbuf, COLOR_BUF_SIZE, "%%{F%s}", value);
    else if (output_format == O_TERM) {
        /* The escape-sequence for color is <CSI><col>;1m (bright/bold
         * output), where col is a 3-bit rgb-value with b in the
         * least-significant bit.  We round the given color to the nearest
         * 3-bit-depth color and output the escape-sequence.
         *
         * The color is "#RRGGBB", i.e. a 24-bit value whose *low* byte is
         * blue.  The variable names below reflect which channel each
         * extracted bit actually is; the ANSI encoding is then assembled
         * as (red << 2) | (green << 1) | blue. */
        long rgb = strtol(value + 1, NULL, 16);
        int red = (rgb & (0xFF << 16)) ? 1 : 0;
        int green = (rgb & (0xFF << 8)) ? 1 : 0;
        int blue = (rgb & (0xFF << 0)) ? 1 : 0;
        int col = (red << 2) | (green << 1) | blue;
        (void)snprintf(colorbuf, COLOR_BUF_SIZE, "\033[3%d;1m", col);
    }

    return colorbuf;
}

/*
 * Some color formats (xmobar) require to terminate colors again
 *
 */
char *endcolor(void) {
    if (output_format == O_XMOBAR)
        return "</fc>";
    else if (output_format == O_TERM)
        return "\033[0m";
    else
        return "";
}

void print_separator(const char *separator) {
    if (output_format == O_I3BAR || strlen(separator) == 0)
        return;

    if (output_format == O_DZEN2)
        printf("^fg(%s)%s^fg()", cfg_getstr(cfg_general, "color_separator"), separator);
    else if (output_format == O_XMOBAR)
        printf("<fc=%s>%s</fc>", cfg_getstr(cfg_general, "color_separator"), separator);
    else if (output_format == O_LEMONBAR)
        printf("%%{F%s}%s%%{F-}", cfg_getstr(cfg_general, "color_separator"), separator);
    else if (output_format == O_TERM)
        printf("%s%s%s", color("color_separator"), separator, endcolor());
    else if (output_format == O_NONE)
        printf("%s", separator);
}

/*
 * The term-output hides the cursor. We call this on exit to reset that.
 */
void reset_cursor(void) {
    printf("\033[?25h");
}

/*
 * Escapes ampersand, less-than, greater-than, single-quote, and double-quote
 * characters with the corresponding Pango markup strings if markup is enabled.
 * See the glib implementation:
 * https://git.gnome.org/browse/glib/tree/glib/gmarkup.c?id=03db1f455b4265654e237d2ad55464b4113cba8a#n2142
 *
 * `buffer` is always NUL-terminated on return, even when the escaped text
 * does not fit; in that case the output is truncated at an entity boundary.
 */
void maybe_escape_markup(char *text, char *buffer, size_t size) {
    if (buffer == NULL || size == 0)
        return;

    /* Maximum number of characters we may write, excluding the NUL. */
    const size_t cap = size - 1;

    if (markup_format == M_NONE) {
        size_t n = strlen(text);
        if (n > cap)
            n = cap;
        memcpy(buffer, text, n);
        buffer[n] = '\0';
        return;
    }

    size_t i = 0;
    for (; *text != '\0' && i < cap; text++) {
        size_t avail = cap - i; /* >= 1 */
        int written = 0;

        switch (*text) {
            case '&':
                written = snprintf(&buffer[i], avail + 1, "&amp;");
                break;
            case '<':
                written = snprintf(&buffer[i], avail + 1, "&lt;");
                break;
            case '>':
                written = snprintf(&buffer[i], avail + 1, "&gt;");
                break;
            case '\'':
                written = snprintf(&buffer[i], avail + 1, "&apos;");
                break;
            case '"':
                written = snprintf(&buffer[i], avail + 1, "&quot;");
                break;
            default:
                if ((0x1 <= *text && *text <= 0x8) ||
                    (0xb <= *text && *text <= 0xc) ||
                    (0xe <= *text && *text <= 0x1f)) {
                    written = snprintf(&buffer[i], avail + 1, "&#x%x;", *text);
                } else {
                    buffer[i] = *text;
                    i++;
                    continue;
                }
                break;
        }

        /*
         * snprintf() returns the would-be length.  If it exceeds `avail`,
         * the entity was truncated; we stop here so we never emit a broken
         * entity, and `i` is moved to the end so the final NUL is placed
         * there.
         */
        if (written < 0 || (size_t)written > avail) {
            i = cap;
            break;
        }
        i += (size_t)written;
    }

    buffer[i] = '\0';
}

/*
 * remove leading spaces
 *
 * The argument is cast to unsigned char before isspace(): passing a negative
 * char (bytes >= 0x80 on signed-char platforms) to isspace() is undefined
 * behavior per POSIX.
 */
char *ltrim(const char *s) {
    while (isspace((unsigned char)*s))
        ++s;
    return sstrdup(s);
}

/*
 * remove trailing spaces
 */
char *rtrim(const char *s) {
    char *r = sstrdup(s);
    if (r == NULL)
        return NULL;

    size_t len = strlen(r);
    if (len == 0)
        return r;

    /*
     * BUG FIX: bounds-check before dereferencing.  The previous loop tested
     * isspace(*fr) / *fr before verifying fr >= r, which read one byte
     * before the start of the allocation for an all-whitespace string —
     * undefined behavior flagged by ASan/UBSan.
     */
    char *end = r + len - 1;
    while (end >= r && isspace((unsigned char)*end))
        --end;
    *(end + 1) = '\0';

    return r;
}

/*
 * remove leading & trailing spaces
 */
char *trim(const char *s) {
    char *r = rtrim(s);
    char *f = ltrim(r);
    free(r);
    return f;
}