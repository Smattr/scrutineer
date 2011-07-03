/* scrutineer, a Makefile validator.
 *
 * Run `scrutineer -h` for usage information.
 *
 * This code is licensed under a CC BY-SA 3.0 licence. For more information see
 * the accompanying README.
 * Matthew Fernandez.
 */
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

/* Helper macro for bailing in the case of an unrecoverable error. */
#define DIE(...) \
    do { \
        fprintf(stderr, __VA_ARGS__); \
        exit(1); \
    } while (0)

#define DEFAULT_CLEAN_TARGET "clean"

typedef struct list {
    const char *value;
    struct list *next;
    int phony; /* Whether this target is .PHONY or not. */
} list_t;

/* Sets the modified time of a file. Returns 0 on success or -1 on failure.
 */
int touch(const char *path, const time_t timestamp) {
    const struct utimbuf t = {
        .actime = timestamp,
        .modtime = timestamp,
    };
    return utime(path, &t);
}

time_t get_mtime(const char *path) {
    struct stat buf;
    int ret;

    ret = stat(path, &buf);
    return ret ? (time_t)0 : buf.st_mtime;
}

inline int not_exists(const char *path) {
    return access(path, F_OK) ? errno : 0;
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

/* Run the given command and return the exit code. */
int run(char *const argv[]) {
    pid_t proc;

#ifndef NDEBUG
    /* Check the arguments we're about to exec are NULL-terminated. */
    int i = 0;
    while (argv[i++]);
#endif

    /* Without flushing stdout/stderr before forking, both parent and child
     * process inherit anything in the buffers and eventually end up flushing
     * (two copies of) it.
     */
    fflush(stdout);
    fflush(stderr);

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
        exit(1);
    } else if (proc > 0) {
        /* Parent process. */
        int status;

        switch (wait(&status)) {
            case -1:
                /* Terminated by signal to me. Fall through. */
            case 0: {
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
        return errno;
    }
}


int main(int argc, char **argv) {
    time_t now, old;
    list_t *p, *p1;
    const char *clean = DEFAULT_CLEAN_TARGET;
    char *args[3];
    char *clean_args[3];
    int c;
    int output_phony = 0;

    /* A list of potential dependencies for each target. */
    list_t *components = NULL;

    /* A list of targets to assess. */
    list_t *targets = NULL;

    /* Parse the command line arguments. */
    while ((c = getopt(argc, argv, "t:c:ph")) != -1) {
        switch (c) {
            case 't': { /* target */
                list_t *temp;
                temp = (list_t*) malloc(sizeof(list_t));
                temp->value = optarg;
                temp->next = targets;
                targets = temp;
                break;
            } case 'c': { /* component */
                list_t *temp;
                temp = (list_t*) malloc(sizeof(list_t));
                temp->value = optarg;
                temp->next = components;
                components = temp;
                break;
            } case 'h': { /* help */
                printf("Usage: %s options\n"
                    " -c component A file to consider as a potential dependency.\n"
                    " -h           Print usage information and exit.\n"
                    " -p           Include .PHONY target after assessing real ones.\n"
                    " -t target    A Makefile target to assess.\n",
                    argv[0]);
                return 0;
            } case 'p': { /* output PHONY rule. */
                output_phony = 1;
                break;
            } case '?': { /* Unknown option. */
                DIE("Unknown option %c.\n", c);
                break;
            } default: { /* getopt failure */
                DIE("Failed to parse command line arguments.\n");
                break;
            }
        }
    }

    if (!targets) {
        DIE("No targets specified.\n");
    }

    if (!components) {
        DIE("No components specified.\n");
    }

    /* Setup clean arguments. */
    /* TODO: Parameterise make so you can use a different build system. */
    clean_args[0] = (char*) malloc(sizeof(char) * (strlen("make") + 1));
    strcpy(clean_args[0], "make");
    clean_args[1] = (char*)clean;
    assert(strlen(clean) != 0);
    clean_args[2] = NULL;

    /* Setup basic build arguments. */
    args[0] = clean_args[0]; /* Cheat and reuse this because we know both
                              * arrays will only ever be passed to const fns.
                              */
    args[2] = NULL;

    /* Initial clean. */
    assert(clean_args[2] == NULL);
    if (run(clean_args)) {
        DIE("Error: Clean failed.\n");
    }

    /* Check all the components we were passed actually exist. */
    for (p1 = components; p1; p1 = p1->next) {
        assert(p1->value);
        if (not_exists(p1->value)) {
            DIE("Component %s doesn't exist after cleaning. "
                "Is it an intermediate file?\n", p1->value);
        }
    }

    /* Build each target multiple times (touching different files in between)
     * to determine dependencies. Note that the initial build of each target is
     * discarded unless it fails because it tells us nothing about
     * dependencies.
     */
    for (p = targets; p; p = p->next) {

        /* Initial build to set the stage. */
        assert(p->value);
        args[1] = (char*)p->value;
        assert(args[2] == NULL);
        if (run(args)) {
            fprintf(stderr,
                "Warning: Failed to build %s from scratch. Broken %s recipe?\n",
                p->value, p->value);
            continue;
        }

        /* We shouldn't know whether this target is phony yet. */
        assert(!p->phony);

        if (not_exists(p->value)) {
            fprintf(stderr,
                "Warning: %s appears to be PHONY! I can't assess this.\n",
                p->value);
            p->phony = 1;
            continue;
        }

        /* Touch every component so we have a known starting point. */
        now = get_now((time_t)0);
        for (p1 = components; p1; p1 = p1->next) {
            assert(p1->value);
            switch (not_exists(p1->value)) {
                case 0: { /* Component exists. */
                    if (touch(p1->value, now)) {
                        DIE("Could not update timestamp for %s.\n", p1->value);
                    }
                    break;
                } case ENOENT: { /* Component doesn't exist. */
                    fprintf(stderr, "Warning: component %s now doesn't exist, "
                        "although cleaning does not seem to delete it. "
                        "Destructive recipe somewhere in your Makefile?\n",
                        p1->value);
                    break;
                } default: { /* Some other access issue. */
                    DIE("Could not determine access rights for %s.\n", p1->value);
                }
            }
        }

        /* Touch the target to make sure it is considered up to date with
         * respect to all the potential dependencies. Note, this is here
         * because the target may not actually be in the user-provided list of
         * components.
         */
        assert(!not_exists(p->value));
        if (touch(p->value, now)) {
            fprintf(stderr, "Could not update timestamp for %s (cannot "
                "determine dependencies).\n", p->value);
            continue;
        }

        /* The target should not be phony if we've reached this point. */
        assert(!p->phony);

        printf("%s:", p->value);
        old = now; /* The timestamp we've marked each file with. */
        for (p1 = components; p1; p1 = p1->next) {
            now = get_now(old);
            assert(p1->value);
            assert(now > old);
            assert(!not_exists(p1->value));
            assert(get_mtime(p->value) == old);
            touch(p1->value, now);
            if (run(args)) {
                DIE("Error: Failed to build %s after touching %s.\n", p->value,
                    p1->value);
            }
            if (not_exists(p->value)) {
                DIE("Error: %s, that was NOT a phony target, was removed when "
                    "building after touching %s. Broken recipe for %s?\n",
                    p->value, p1->value, p->value);
            }
            now = get_mtime(p->value);
            assert(now >= old); /* Check we haven't gone back in time. */
            if (now != old) {
                /* The target was rebuilt. */
                printf(" %s", p1->value);
                old = now;
            }
        }
        printf("\n");

        /* Clean up. */
        assert(clean_args[2] == NULL);
        if (run(clean_args)) {
            DIE("Error: Clean failed.\n");
        }
    }

    if (output_phony) {
        int marker;

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
    }

    return 0;
}
