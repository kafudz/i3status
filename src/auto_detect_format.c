/*
 * vim:ts=4:sw=4:expandtab
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include "i3status.h"

/*
 * Reads /proc/<pid>/stat and returns (via pointers) the name and parent pid of
 * the specified pid.
 *
 * On success, *outname points to a freshly malloc()ed NUL-terminated string
 * that the caller must free().  On failure both outputs are left untouched
 * and false is returned.
 */
static bool parse_proc_stat(pid_t pid, char **outname, pid_t *outppid) {
    char path[255];
    /* the relevant contents (for us) are:
     * <pid> (<program name>) <status> <ppid>
     * which should well fit into one page of 4096 bytes */
    char buffer[4096];

    /*
     * snprintf() returns the would-be length; since path[] is much larger
     * than "/proc/<pid>/stat" can ever be, truncation is impossible, but
     * check correctly anyway (the old `== -1` test never fires for
     * truncation — snprintf returns sizeof(path)-1 in that case).
     */
    int rc = snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if (rc < 0 || (size_t)rc >= sizeof(path))
        return false;
    if (!slurp(path, buffer, sizeof(buffer)))
        return false;

    char *leftbracket = strchr(buffer, '(');
    char *rightbracket = strrchr(buffer, ')');
    if (!leftbracket || !rightbracket || rightbracket <= leftbracket)
        return false;
    if (sscanf(rightbracket + 2, "%*c %d", outppid) != 1)
        return false;

    *rightbracket = '\0';
    char *name = strdup(leftbracket + 1);
    if (name == NULL)
        return false;

    *outname = name;
    return true;
}

/*
 * Map a process comm name to the i3status output format it implies, or
 * NULL if the process is not a known status-line consumer.
 *
 * The returned pointer is to a static string literal; do not free() it.
 */
static const char *format_for_process(const char *name) {
    if (strcasecmp(name, "i3bar") == 0 || strcasecmp(name, "swaybar") == 0)
        return "i3bar";
    else if (strcasecmp(name, "dzen2") == 0)
        return "dzen2";
    else if (strcasecmp(name, "xmobar") == 0)
        return "xmobar";
    else
        return NULL;
}

/*
 * This function tries to automatically find out where i3status is being piped
 * to and choses the appropriate output format.
 *
 * It is a little hackish but should work for most setups :).
 *
 * By iterating through /proc/<number>/stat and finding out the parent process
 * id (just like pstree(1) or ps(1) work), we can get all children of our
 * parent. When the output of i3status is being piped somewhere, the shell
 * (parent process) spawns i3status and the destination process, so we will
 * find our own process and the pipe target.
 *
 * We then check whether the pipe target's name is known and chose the format.
 */
char *auto_detect_format(void) {
    /* If stdout is a tty, we output directly to a terminal. */
    if (isatty(STDOUT_FILENO)) {
        return "term";
    }

    pid_t myppid = getppid();
    pid_t mypid = getpid();

    DIR *dir;
    struct dirent *entry;

    /* Points into a static string literal, never heap. */
    const char *format = NULL;

    /* Always initialize before any early return, so a future refactor that
     * falls through to `out:` can't free an indeterminate pointer. */
    char *parentname = NULL;
    pid_t parentpid = 0;

    if (!parse_proc_stat(myppid, &parentname, &parentpid))
        return NULL;

    /*
     * When the shell runs a pipeline, some shells (dash-as-sh, ksh, mksh)
     * exec the last command, making the pipe target the parent of i3status
     * rather than a sibling.  zsh and bash typically fork instead.
     */
    if (strcmp(parentname, "sh") == 0) {
        pid_t tmp_ppid = parentpid;
        free(parentname);
        parentname = NULL;
        fprintf(stderr, "i3status: auto-detection: parent process is \"sh\", looking at its parent\n");
        if (!parse_proc_stat(tmp_ppid, &parentname, &parentpid))
            return NULL;
    }

    /* If the pipe target is directly our parent, we are done. */
    if ((format = format_for_process(parentname)) != NULL)
        goto out;

    if (!(dir = opendir("/proc")))
        goto out;

    while ((entry = readdir(dir)) != NULL) {
        pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid == 0 || pid == mypid)
            continue;

        char *name = NULL;
        pid_t ppid = 0;
        int loopcnt = 0;
        /*
         * Find out the name of this sibling process.  To avoid the possible
         * race condition of the process existing already but not executing
         * the destination (shell after fork() and before exec()), we keep
         * retrying while the child's comm still equals the shell's name.
         * We try for up to ~0.5 s, then give up.
         */
        do {
            /* give the scheduler a chance between each iteration */
            if (name) {
                usleep(50);
                free(name);
                name = NULL;
            }

            if (!parse_proc_stat(pid, &name, &ppid))
                break;
            if (ppid != myppid)
                break;
        } while (strcmp(parentname, name) == 0 && loopcnt++ < 10000);

        if (!name)
            continue;

        /* Check for known destination programs and set format. */
        const char *newfmt = format_for_process(name);
        free(name);

        /*
         * BUG FIX: unrelated child processes (anything we don't recognize,
         * e.g. an editor, another shell) must not reset an already-detected
         * format.  Previously `format = newfmt` in the else-branch clobbered
         * `format` to NULL whenever an unrecognized sibling was encountered,
         * which happens constantly.  Only act when we actually recognize
         * the process.
         */
        if (newfmt == NULL)
            continue;

        if (format != NULL && strcmp(newfmt, format) != 0) {
            fprintf(stderr,
                    "i3status: cannot auto-configure, situation ambiguous "
                    "(format \"%s\" *and* \"%s\" detected)\n",
                    format, newfmt);
            format = NULL;
            break;
        }
        format = newfmt;
    }

    closedir(dir);

out:
    free(parentname);

    /* The public API returns char *; the literals are never mutated. */
    return (char *)format;
}