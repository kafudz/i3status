// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <sys/types.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <fcntl.h>    /* was <sys/fcntl.h> — nonstandard alias */
#include <sys/stat.h>

#include "i3status.h"

#define exit_if_null(pointer, ...) \
    {                              \
        if (pointer == NULL)       \
            die(__VA_ARGS__);      \
    }

/*
 * Reads at most (size - 1) bytes from `filename` into `destination` and
 * NUL-terminates it.
 *
 * On success, true is returned and `destination` holds a NUL-terminated
 * string of up to (size - 1) bytes (contents truncated if the file is
 * larger).  On failure, false is returned and the contents of `destination`
 * are left untouched.
 *
 * A short read caused by EINTR (e.g. a SIGUSR1 refresh) is retried; this
 * matters because i3status deliberately interrupts syscalls with SIGUSR1.
 */
bool slurp(const char *filename, char *destination, int size) {
    if (destination == NULL || size <= 0)
        return false;

    int fd;
    if ((fd = open(filename, O_RDONLY)) == -1)
        return false;

    /* We need one byte for the trailing NUL byte. */
    ssize_t n;
    do {
        n = read(fd, destination, (size_t)(size - 1));
    } while (n == -1 && errno == EINTR);

    if (n >= 0)
        destination[n] = '\0';
    (void)close(fd);

    return n >= 0;
}

/*
 * This function resolves ~ in pathnames.
 * It may resolve wildcards in the first part of the path, but if no match
 * or multiple matches are found, it just returns a copy of path as given.
 *
 * The caller owns the returned string and must free() it.
 */
char *resolve_tilde(const char *path) {
    /*
     * Use a *local* glob_t, not a static one: a static would be shared
     * across calls and make globfree() on a partially-initialized struct
     * a real hazard.  We also only call globfree() when glob() succeeded
     * (POSIX leaves the struct unspecified on failure, so freeing it is
     * undefined behaviour even though glibc happens to tolerate it).
     */
    glob_t globbuf;
    memset(&globbuf, 0, sizeof(globbuf));

    char *result = NULL;
    const char *tail = strchr(path, '/');
    size_t head_len = tail ? (size_t)(tail - path) : strlen(path);

    char *head = strndup(path, head_len);
    if (head == NULL)
        die("Error: out of memory (strndup())\n");

    int res = glob(head, GLOB_TILDE, NULL, &globbuf);
    free(head);

    if (res == GLOB_NOMATCH || globbuf.gl_pathc != 1) {
        /* No match, or many wildcard matches: use the path unchanged. */
        result = sstrdup(path);
    } else if (res != 0) {
        /* GLOB_ABORTED, GLOB_NOSPACE, or an unknown error. */
        globfree(&globbuf);
        die("glob() failed");
    } else {
        const char *expanded = globbuf.gl_pathv[0];
        size_t total = strlen(expanded) + (tail ? strlen(tail) : 0);
        result = scalloc(total + 1);
        /* Copy while globbuf is still alive: globfree() below invalidates
         * gl_pathv[0]. */
        strcpy(result, expanded);
        if (tail)
            strcat(result, tail);
    }

    globfree(&globbuf);
    return result;
}

char *sstrdup(const char *str) {
    if (str == NULL)
        return NULL;
    char *result = strdup(str);
    exit_if_null(result, "Error: out of memory (strdup())\n");
    return result;
}

void *scalloc(size_t size) {
    void *result = calloc(size, 1);
    exit_if_null(result, "Error: out of memory (calloc(%zu))\n", size);
    return result;
}

/*
 * Returns a pointer to the `amount`-th occurrence of `character` in
 * `input`, or `input` itself if `amount` is 0 or `input` is empty.
 *
 * NOTE: despite the name, the returned pointer points *at* the matched
 * character (e.g. the space), not past it.  Callers rely on this so that
 * atoi()/strtod() can skip the leading whitespace themselves.  If there
 * are fewer than `amount` occurrences, the pointer to the last character
 * of the string is returned.
 *
 * The comment previously claimed "first non-'character' character", which
 * does not match the implementation.  Renaming would touch all callers, so
 * the comment is corrected instead.
 */
char *skip_character(char *input, char character, int amount) {
    if (input == NULL || amount < 0)
        return input;

    char *walk;
    size_t len = strlen(input);
    int blanks = 0;

    for (walk = input; ((size_t)(walk - input) < len) && (blanks < amount); walk++)
        if (*walk == character)
            blanks++;

    return (walk == input ? walk : walk - 1);
}

/*
 * Write an error message to stderr and exit.  Marked noreturn so the
 * compiler knows control does not continue past this call.
 */
_Noreturn void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}