#include <utime.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DEFAULT_CLEAN_TARGET "clean"

typedef struct list {
    char *value;
    struct list *next;
    int phony; /* Whether this target is .PHONY or not. */
} list_t;

/* A list of potential dependencies for each target. */
list_t *components = NULL;

/* A list of targets to assess. */
list_t *targets = NULL;

/* Sets the modified time of a file. Returns 0 on success or -1 on failure.
 */
int touch(const char *path, const time_t timestamp) {
    struct utimbuf t = {
        .actime = timestamp,
        .modtime = timestamp,
    };
    return utime(path, &t);
}

time_t get_mtime(const char *path) {
    struct stat buf;
    stat(path, &buf); /* TODO: Check the return code of stat. */
    return buf.st_mtime;
}

inline int exists(const char *path) {
    return !access(path, F_OK);
}

/* Returns a time approximating now that is not the value not. The idea behind
 * this is that we need a value that is in the future (with respect to not),
 * but we don't care how far in the future.
 */
time_t get_now(time_t not) {
    time_t ret;
    while ((ret = time(NULL)) <= not) {
        usleep(100);
    }
    return ret;
}

/* Run the given command and return the return code. */
int run(char *const argv[]) {
    pid_t proc;

    proc = fork();
    if (proc == 0) {
        /* Child process. */
        execvp(argv[0], argv);
        /* If we reach this point execvp failed. */
        return errno;
    } else if (proc > 0) {
        /* Parent process. */
        int status;

        switch (wait(&status)) {
            case -1: {
                /* Terminated by signal to me. */
                return errno;
                break;
            } case 0: {
                /* Status unavailable. */
                return errno;
                break;
            } default: {
                /* wait returned proc; expected. */
                return status;
                break;
            }
        }
    } else {
        /* Fork failed. */
        /* TODO: Do something here... */
        return errno;
    }
}


int main(int argc, char **argv) {
    time_t now, old;
    list_t *p, *p1;
    const char *clean = DEFAULT_CLEAN_TARGET;
    char *args[3];
    char *clean_args[3];
    int marker;

    /* TODO: Some magic to set components and targets from cmdline. */

    /* Setup clean arguments. */
    /* TODO: Parameterise make so you can use a different build system. */
    clean_args[0] = (char*) malloc(sizeof(char) * strlen("make") + 1);
    strcpy(clean_args[0], "make");
    clean_args[1] = clean;
    assert(strlen(clean) != 0);
    clean_args[2] = NULL;

    /* Setup basic build arguments. */
    args[0] = clean_args[0]; /* Cheat and reuse this because we know both
                              * arrays will only ever be passed to const fns.
                              */
    args[2] = NULL;

    /* Build each target multiple times (touching different files in between)
     * to determine dependencies. Note that the initial build of each target is
     * discarded unless it fails because it tells us nothing about
     * dependencies.
     */
    for (p = targets; p; p = p->next) {

        /* Clean up from the last build (also don't assume the user has left
         * the build directory in a clean state when they executed scrutineer.
         */
        assert(clean_args[2] == NULL);
        if (run(clean_args)) {
            fprintf(stderr, "Error: Clean failed.\n");
            return -1;
        }
        
        /* First build to set the stage. */
        assert(p->value);
        args[1] = p->value;
        assert(args[2] == NULL);
        if (run(args)) {
            fprintf(stderr, "Warning: Failed to build %s from scratch. Broken %s recipe?\n", p->value, p->value);
            continue;
        }

        assert(!p->phony); /* We shouldn't know whether this target is phony yet. */
        if (!exists(p->value)) {
            fprintf(stderr, "Warning: PHONY target %s! I can't assess this.\n", p->value);
            p->phony = 1;
            continue;
        }

        /* Touch every component so we have a known "clean" starting point. */
        now = get_now((time_t)0);
        for (p = components; p; p = p->next) {
            assert(p->value);
            if (exists(p->value)) {
                if (touch(p->value, now)) {
                    fprintf(stderr, "Could not update timestamp for %s.\n", p->value);
                    return -1;
                }
            } else if (errno != ENOENT) {
                fprintf(stderr, "Could not determine access rights for %s.\n", p->value);
            }
        }

        printf("%s:", p->value);
        old = now;
        now = get_now(now);
        for (p1 = components; p1; p1 = p1->next) {
            assert(p1->value);
            assert(now > old);
            touch(p1->value, now);
            if (run(args)) {
                fprintf(stderr, "Warning: Failed to build %s after touching %s.\n", p->value, p1->value);
                continue;
            }
            if (!exists(p->value)) {
                fprintf(stderr, "Warning: %s, that was NOT a phony target, was removed when building after touching %s. Broken recipe for %s?\n", p->value, p1->value, p->value);
                break;
            }
            if (get_mtime(p->value) != old) {
                /* The target was rebuilt. */
                printf(" %s", p1->value);
            }
        }
        printf("\n\n");
    }

    /* TODO: Command line option to disable outputting the phony rule. */
    for (marker = 0, p = targets; p; p = p->next) {
        if (p->phony) {
            if (!marker) {
                printf(".PHONY:");
                marker = 1;
            }
            printf(" %s", p->value);
        }
    }
    /* If we found at least one phony target. */
    if (marker) printf("\n");

    return 0;
}
