// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdbool.h>
#include <ctype.h>
#include <glob.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "i3status.h"

/*
<<<<<<< HEAD
 * Reads a PID from the given file and checks whether that process exists by
 * sending signal 0 (does not do anything). kill() will return ESRCH if the
 * process does not exist and 0 or EPERM (depending on the uid) if it exists.
 *
 * The file content is validated first: kill() treats 0 and negative values
 * specially (0 = our own process group, -1 = every process we may signal), so
 * an empty or garbage pidfile must never be passed on to it, or the module
 * would report "running" for it.
 */
static bool pidfile_process_runs(const char *pidfile) {
    char pidbuf[16];

    if (!slurp(pidfile, pidbuf, sizeof(pidbuf)))
        return false;

    errno = 0;
    char *end = NULL;
    const long value = strtol(pidbuf, &end, 10);
    if (end == pidbuf || errno == ERANGE)
        return false; /* no number at all, or out of range */

    /* allow trailing whitespace (e.g. the newline), but nothing else */
    while (isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return false;

    const pid_t pid = (pid_t)value;
    if (value <= 0 || (long)pid != value)
        return false;

    return (kill(pid, 0) == 0 || errno == EPERM);
}

/*
 * Checks if the PID in path is still valid.
=======
 * Returns whether the process whose PID is stored in the given buffer exists.
 *
 * kill() with signal 0 does not do anything: it will return ESRCH if the
 * process does not exist and 0 or EPERM (depending on the uid) if it exists.
 *
 * PIDs that are not positive are rejected up front, because kill() gives them
 * a special meaning (0: our own process group, -1: every process we may
 * signal), which would make a garbled or empty PID file look like a running
 * process.
 *
 */
static bool pid_buffer_runs(const char *pidbuf) {
    const long pid = strtol(pidbuf, NULL, 10);
    if (pid <= 0)
        return false;
    return (kill((pid_t)pid, 0) == 0 || errno == EPERM);
}

/*
 * Checks if the PID in path is still valid by sending signal 0 (does not do
 * anything).
>>>>>>> 8e7bd0df670daf3f480461efd8f0546a5cef3f5c
 *
 * If multiple files match the glob pattern, all of them will be checked until
 * the first running process is found. A file that cannot be read or does not
 * contain a valid PID is skipped, it does not stop the search.
 *
 */
bool process_runs(const char *path) {
<<<<<<< HEAD
    glob_t globbuf;

    /* GLOB_NOCHECK returns the pattern itself when nothing matches, so a
     * path without wildcards is handled by the same loop as a pattern. */
    const int ret = glob(path, GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf);
    if (ret != 0) {
        /* e.g. out of memory; not worth killing the whole status bar */
        fprintf(stderr, "i3status: glob(\"%s\") failed (%d)\n", path, ret);
        globfree(&globbuf);
        return false;
    }

    bool running = false;
    for (size_t i = 0; i < globbuf.gl_pathc && !running; i++)
        running = pidfile_process_runs(globbuf.gl_pathv[i]);

    globfree(&globbuf);
=======
    static char pidbuf[16];
    glob_t globbuf;
    memset(pidbuf, 0, sizeof(pidbuf));
    memset(&globbuf, 0, sizeof(globbuf));

    if (glob(path, GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf) != 0)
        die("glob() failed\n");

    bool running = false;
    for (size_t i = 0; i < globbuf.gl_pathc && !running; i++) {
        /* A file we cannot read (e.g. a stale one) must not hide the others. */
        if (!slurp(globbuf.gl_pathv[i], pidbuf, sizeof(pidbuf)))
            continue;
        running = pid_buffer_runs(pidbuf);
    }
    globfree(&globbuf);

>>>>>>> 8e7bd0df670daf3f480461efd8f0546a5cef3f5c
    return running;
}
