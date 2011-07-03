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
#include <string.h>
#include <ctype.h>

#ifdef __GNUC__
    /* If we're using a GNU compiler there are some optimisation hints we can
     * use.
     */
    #define NORETURN __attribute__((noreturn))
#else
    #define NORETURN /* Nothing */
#endif

#define DEFAULT_CLEAN_TARGET "clean"

typedef struct list {
    char *value;
    struct list *next;
    int phony; /* Whether this target is .PHONY or not. */
} list_t;

void die(const char *message) NORETURN;

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

        /* Supress our output. */
        stdout = freopen("/dev/null", "w", stdout);
        assert(stdout);
        stderr = freopen("/dev/null", "w", stderr);
        assert(stderr);
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
    int c;

    /* Parse the command line arguments. */
    while ((c = getopt(argc, argv, "t:c:")) != -1) {
        switch (c) {
            case 't': { /* target */
                list_t *temp;
                temp = (list_t*) malloc(sizeof(list_t));
                temp->value = optarg;
                if (targets) { /* List has existing elements. */
                    temp->next = targets;
                } else {
                    temp->next = NULL;
                }
                targets = temp;
                break;
            } case 'c': { /* component */
                list_t *temp; /* FIXME: Duplicate code == yuck ? */
                temp = (list_t*) malloc(sizeof(list_t));
                temp->value = optarg;
                if (components) { /* List has existing elements. */
                    temp->next = components;
                } else {
                    temp->next = NULL;
                }
                components = temp;
                break;
            } case '?': { /* Unknown option. */
                char *message;

                message = (char*) malloc(sizeof(char) * (strlen("Unknown option .") + 2));
                sprintf(message, "Unknown option %c.", c);
                die(message);
                break;
            } default: { /* getopt failure */
                assert(0); /* FIXME: More sensible failure? */
                return -1;
            }
        }
    }

    if (!targets) {
        die("No targets provided.");
    }

    if (!components) {
        die("No components provided.");
    }

    /* Setup clean arguments. */
    /* TODO: Parameterise make so you can use a different build system. */
    clean_args[0] = (char*) malloc(sizeof(char) * (strlen("make") + 1));
    strcpy(clean_args[0], "make");
    clean_args[1] = (char*)clean; /* FIXME: Casting the const away here is yuck++. */
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
            die("Error: Clean failed.");
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
        for (p1 = components; p1; p1 = p1->next) {
            assert(p1->value);
            if (exists(p1->value)) {
                if (touch(p1->value, now)) {
                    char *message;

                    message = (char*) malloc(sizeof(char) * (strlen("Could not update timestamp for .") + strlen(p1->value) + 1));
                    sprintf(message, "Could not update timestamp for %s.", p1->value);
                    die(message);
                }
            } else if (errno != ENOENT) {
                char *message;

                message = (char*) malloc(sizeof(char) * (strlen("Could not determine access rights for .") + strlen(p1->value) + 1));
                sprintf(message, "Could not determine access rights for %s.", p1->value);
                die(message);
            }
        }

        /* Touch the target to make sure it is considered up to date with
         * respect to all the potential dependencies. Note, this is here
         * because the target, if not phony, may not actually be in the
         * user-provided list of components.
         */
        if (exists(p->value)) {
            if (touch(p->value, now)) {
                fprintf(stderr, "Could not update timestamp for %s (cannot determine dependencies).\n", p->value);
                continue;
            }
        }

        /* The target should not be phony if we've reached this point. */
        assert(!p->phony);

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

void die(const char *message) {
    assert(message);
    fprintf(stderr, "%s\n", message);
    exit(1);
}
