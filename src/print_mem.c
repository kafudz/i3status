// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include "i3status.h"

#define MAX_DECIMALS 4
/* "1023.9999 TiB" is 13 characters; leave generous room so that no
 * combination of value, unit and precision can overflow the buffers. */
#define STRING_SIZE 32

#define BINARY_BASE 1024ULL

#ifndef MEMINFO_PATH
#define MEMINFO_PATH "/proc/meminfo"
#endif

#if defined(__linux__)
static const char *const iec_symbols[] = {"B", "KiB", "MiB", "GiB", "TiB"};
#define MAX_EXPONENT ((sizeof iec_symbols / sizeof *iec_symbols) - 1)

/* Plain 64-bit arithmetic helpers: unsigned long is only 32 bits wide on some
 * platforms, and unsigned subtraction/multiplication must not wrap around. */
static unsigned long long sub_sat(unsigned long long a, unsigned long long b) {
    return a > b ? a - b : 0;
}

static unsigned long long mul_sat(unsigned long long a, unsigned long long b) {
    if (b != 0 && a > ULLONG_MAX / b)
        return ULLONG_MAX;
    return a * b;
}

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

/*
 * Prints the given amount of bytes in a human readable manner.
 *
 */
static void print_bytes_human(char *out, size_t outsize, unsigned long long bytes, const char *unit, const int decimals) {
    double base = (double)bytes;
    size_t exponent = 0;
    while (base >= BINARY_BASE && exponent < MAX_EXPONENT) {
        if (unit != NULL && strcasecmp(unit, iec_symbols[exponent]) == 0) {
            break;
        }

        base /= BINARY_BASE;
        exponent += 1;
    }
    /* A negative precision would be treated by printf as "not given" (6
     * digits), which is longer than the buffers were sized for. */
    const int prec = decimals < 0 ? 0 : (decimals > MAX_DECIMALS ? MAX_DECIMALS : decimals);
    snprintf(out, outsize, "%.*f %s", prec, base, iec_symbols[exponent]);
}

static void print_percentage(char *out, size_t outsize, double percent) {
    snprintf(out, outsize, "%.1f%s", percent, pct_mark);
}

/*
 * Convert a string to its absolute representation based on the total
 * memory of `mem_total`.
 *
 * The string can contain any percentage values, which then return a
 * the value of `mem_amount` in relation to `mem_total`.
 * Alternatively an absolute value can be given, suffixed with an iec
 * symbol.
 *
 */
static unsigned long long memory_absolute(const char *mem_amount, const unsigned long long mem_total) {
    char *endptr;
    errno = 0;
    unsigned long long amount = strtoull(mem_amount, &endptr, 10);
    if (endptr == mem_amount)
        return 0; /* not a number */
    if (errno == ERANGE)
        amount = ULLONG_MAX;

    while (endptr[0] != '\0' && isspace((unsigned char)endptr[0]))
        endptr++;

    switch (endptr[0]) {
        case 'T':
        case 't':
            amount = mul_sat(amount, BINARY_BASE);
            /* fall-through */
        case 'G':
        case 'g':
            amount = mul_sat(amount, BINARY_BASE);
            /* fall-through */
        case 'M':
        case 'm':
            amount = mul_sat(amount, BINARY_BASE);
            /* fall-through */
        case 'K':
        case 'k':
            amount = mul_sat(amount, BINARY_BASE);
            break;
        case '%':
            if (amount > 100)
                amount = 100;
            amount = (unsigned long long)((long double)mem_total * (long double)amount / 100.0L);
            break;
    }

    return amount;
}

/*
 * If `line` starts with `key`, stores the (kB) value that follows into *dst
 * and returns true.
 */
static bool meminfo_field(const char *line, const char *key, unsigned long long *dst) {
    if (!BEGINS_WITH(line, key))
        return false;
    *dst = strtoull(line + strlen(key), NULL, 10);
    return true;
}
#endif

void print_memory(memory_ctx_t *ctx) {
    char *outwalk = ctx->buf;

#if defined(__linux__)
    const char *selected_format = ctx->format;
    const char *output_color = NULL;
    const char *method = ctx->memory_used_method ? ctx->memory_used_method : "classical";
    const char *unit = ctx->unit ? ctx->unit : "auto";

    unsigned long long ram_total = 0;
    unsigned long long ram_free = 0;
    unsigned long long ram_available = 0;
    unsigned long long ram_buffers = 0;
    unsigned long long ram_cached = 0;
    unsigned long long ram_shared = 0;
    bool have_total = false;
    bool have_free = false;
    bool have_available = false;

    FILE *file = fopen(MEMINFO_PATH, "r");
    if (!file) {
        goto error;
    }
    for (char line[128]; fgets(line, sizeof line, file);) {
        if (meminfo_field(line, "MemTotal:", &ram_total))
            have_total = true;
        else if (meminfo_field(line, "MemFree:", &ram_free))
            have_free = true;
        else if (meminfo_field(line, "MemAvailable:", &ram_available))
            have_available = true;
        else if (!meminfo_field(line, "Buffers:", &ram_buffers) &&
                 !meminfo_field(line, "Cached:", &ram_cached))
            (void)meminfo_field(line, "Shmem:", &ram_shared);
    }
    fclose(file);

    /* MemTotal and MemFree must be there. The others can be missing (very
     * old kernels, containers), in which case they are treated as 0 -- except
     * MemAvailable (added in Linux 3.14), which is estimated. */
    if (!have_total || !have_free || ram_total == 0) {
        goto error;
    }

    // Values are in kB, convert them to B.
    ram_total = mul_sat(ram_total, 1024ULL);
    ram_free = mul_sat(ram_free, 1024ULL);
    ram_available = mul_sat(ram_available, 1024ULL);
    ram_buffers = mul_sat(ram_buffers, 1024ULL);
    ram_cached = mul_sat(ram_cached, 1024ULL);
    ram_shared = mul_sat(ram_shared, 1024ULL);

    if (!have_available)
        ram_available = ram_free + ram_buffers + ram_cached;
    if (ram_available > ram_total)
        ram_available = ram_total;

    unsigned long long ram_used;
    if (BEGINS_WITH(method, "memavailable")) {
        ram_used = sub_sat(ram_total, ram_available);
    } else if (BEGINS_WITH(method, "classical")) {
        ram_used = sub_sat(sub_sat(sub_sat(ram_total, ram_free), ram_buffers), ram_cached);
    } else {
        /* a configuration mistake should not take the whole status bar down */
        OUTPUT_FULL_TEXT("invalid memory_used_method");
        fprintf(stderr, "i3status: Unexpected value: memory_used_method = %s\n", method);
        return;
    }

    if (ctx->threshold_degraded) {
        const unsigned long long threshold = memory_absolute(ctx->threshold_degraded, ram_total);
        if (ram_available < threshold) {
            output_color = "color_degraded";
        }
    }

    if (ctx->threshold_critical) {
        const unsigned long long threshold = memory_absolute(ctx->threshold_critical, ram_total);
        if (ram_available < threshold) {
            output_color = "color_bad";
        }
    }

    if (output_color) {
        START_COLOR(output_color);

        if (ctx->format_degraded)
            selected_format = ctx->format_degraded;
    }

    char string_ram_total[STRING_SIZE];
    char string_ram_used[STRING_SIZE];
    char string_ram_free[STRING_SIZE];
    char string_ram_available[STRING_SIZE];
    char string_ram_shared[STRING_SIZE];
    char string_percentage_free[STRING_SIZE];
    char string_percentage_available[STRING_SIZE];
    char string_percentage_used[STRING_SIZE];
    char string_percentage_shared[STRING_SIZE];

    print_bytes_human(string_ram_total, STRING_SIZE, ram_total, unit, ctx->decimals);
    print_bytes_human(string_ram_used, STRING_SIZE, ram_used, unit, ctx->decimals);
    print_bytes_human(string_ram_free, STRING_SIZE, ram_free, unit, ctx->decimals);
    print_bytes_human(string_ram_available, STRING_SIZE, ram_available, unit, ctx->decimals);
    print_bytes_human(string_ram_shared, STRING_SIZE, ram_shared, unit, ctx->decimals);
    print_percentage(string_percentage_free, STRING_SIZE, 100.0 * ram_free / ram_total);
    print_percentage(string_percentage_available, STRING_SIZE, 100.0 * ram_available / ram_total);
    print_percentage(string_percentage_used, STRING_SIZE, 100.0 * ram_used / ram_total);
    print_percentage(string_percentage_shared, STRING_SIZE, 100.0 * ram_shared / ram_total);

    placeholder_t placeholders[] = {
        {.name = "%total", .value = string_ram_total},
        {.name = "%used", .value = string_ram_used},
        {.name = "%free", .value = string_ram_free},
        {.name = "%available", .value = string_ram_available},
        {.name = "%shared", .value = string_ram_shared},
        {.name = "%percentage_free", .value = string_percentage_free},
        {.name = "%percentage_available", .value = string_percentage_available},
        {.name = "%percentage_used", .value = string_percentage_used},
        {.name = "%percentage_shared", .value = string_percentage_shared}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(selected_format, &placeholders[0], num);
    if (formatted != NULL) {
        /* not OUTPUT_FORMATTED: it advances outwalk by strlen(formatted) even
         * when strncpy truncated, which leaves outwalk past the buffer */
        outwalk = append_clamped(outwalk, ctx->buf, ctx->buflen, formatted);
        free(formatted);
    }

    if (output_color)
        END_COLOR;

    OUTPUT_FULL_TEXT(ctx->buf);

    return;
error:
    OUTPUT_FULL_TEXT("can't read memory");
    fputs("i3status: Cannot read system memory using " MEMINFO_PATH "\n", stderr);
#else
    OUTPUT_FULL_TEXT("");
    fputs("i3status: Memory status information is not supported on this system\n", stderr);
#endif
}
