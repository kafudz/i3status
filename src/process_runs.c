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
 *
 * If multiple files match the glob pattern, all of them will be checked until
 * the first running process is found. A file that cannot be read or does not
 * contain a valid PID is skipped, it does not stop the search.
 *
 */
bool process_runs(const char *path) {
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
    return running;
}
