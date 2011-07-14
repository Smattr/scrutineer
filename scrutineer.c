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

#define DEFAULT_CLEAN "make clean"

typedef struct list {
    const char *value;
    struct list *next;
    int phony; /* Whether this target is .PHONY or not. */
} list_t;

#ifndef _GNU_SOURCE
/* If _GNU_SOURCE is defined then we will already have strndup from string.h.
 */

/* Copies at most n characters to a duplicate string. Result is '\0' terminated.
 */
char *strndup(const char *s, size_t n) {
    char *p;
    size_t i;

    p = (char*)malloc(sizeof(char) * (n + 1));
    for (i = 0; *s != '\0' && i != n; ++i, ++s)
        p[i] = *s;
    p[i] = '\0';

    if (i != n)
        /* We found a '\0' before the requested n characters. */
        p = (char*)realloc(p, sizeof(char) * (i + 1));

    return p;
}
#endif

/* Sets the modified time of a file. Returns 0 on success or -1 on failure.
 */
int touch(const char *path, const time_t timestamp) {
    const struct utimbuf t = {
        .actime = timestamp,
        .modtime = timestamp,
    };
    return utime(path, &t);
}

/* Returns the modified time of a file. */
time_t get_mtime(const char *path) {
    struct stat buf;
    int ret;

    ret = stat(path, &buf);
    return ret ? (time_t)0 : buf.st_mtime;
}

/* Returns 1 if a file exists and 0 otherwise. */
inline int exists(const char *path) {
    return !access(path, F_OK);
}

/* Split a string into an array of words terminated by a null entry.
 * TODO: Cope with quoted strings as words.
 */
char **split(const char *s) {
    unsigned int i, j;
    char **parts = NULL;
    unsigned int sz = 0;

    for (i = 0, j = 0; s[i] != '\0'; ++i) {

        /* Find the next space or end of string. */
        for (j = i; s[j] != '\0' && s[j] != ' '; ++j);

        if (i != j) {
            /* Only add this item if we've found something more than a single
             * space.
             */
            parts = (char**)realloc(parts, sizeof(char**) * (sz + 1));
            parts[sz] = strndup(s + i, j - i);
            ++sz;
        }

        /* If we're at the end of the string setting i=j will cause a buffer
         * overflow in the next iteration of the loop.
         */
        if (s[j] == '\0') break;

        /* Jump the word we've just extracted. */
        i = j;
    }

    /* Append NULL. */
    parts = (char**)realloc(parts, sizeof(char**) * (sz + 1));
    parts[sz] = NULL;

    return parts;
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
        stdin = freopen("/dev/null", "r", stdin);
        assert(stdin);

        (void)execvp(argv[0], argv);

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
    char *args[3];
    char **clean = NULL;
    int c;
    int output_phony = 0;

    /* A list of potential dependencies for each target. */
    list_t *dependencies = NULL;

    /* A list of targets to assess. */
    list_t *targets = NULL;

    /* Parse the command line arguments. */
    while ((c = getopt(argc, argv, "c:t:d:ph")) != -1) {
        switch (c) {
            case 'c': { /* clean action */
                if (clean)
                    DIE("Multiple clean actions specified.\n");
                clean = split(optarg);
                break;
            } case 't': { /* target */
                list_t *temp;
                temp = (list_t*) malloc(sizeof(list_t));
                temp->value = optarg;
                temp->next = targets;
                targets = temp;
                break;
            } case 'd': { /* potential dependency */
                list_t *temp;
                temp = (list_t*) malloc(sizeof(list_t));
                temp->value = optarg;
                temp->next = dependencies;
                dependencies = temp;
                break;
            } case 'h': { /* help */
                printf("Usage: %s options\n"
                    " -c clean     A custom command to clean (default \"make clean\").\n"
                    " -d file      A file to consider as a potential dependency.\n"
                    " -h           Print usage information and exit.\n"
                    " -p           Include .PHONY target after assessing real ones.\n"
                    " -t target    A Makefile target to assess.\n",
                    argv[0]);
                return 0;
            } case 'p': { /* output PHONY rule. */
                output_phony = 1;
                break;
            } case '?': { /* Unknown option. */
                exit(1);
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

    if (!dependencies) {
        DIE("No files specified.\n");
    }

    /* Setup clean arguments. */
    if (!clean)
        clean = split(DEFAULT_CLEAN);

    /* Setup basic build arguments. */
    args[0] = strdup("make");
    args[2] = NULL;

    /* Initial clean. */
    if (run(clean)) {
        DIE("Error: Clean failed.\n");
    }

    /* Check all the files we were passed actually exist. */
    for (p1 = dependencies; p1; p1 = p1->next) {
        assert(p1->value);
        if (!exists(p1->value)) {
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

        if (!exists(p->value)) {
            fprintf(stderr,
                "Warning: %s appears to be PHONY! I can't assess this.\n",
                p->value);
            p->phony = 1;
            continue;
        }

        /* Touch every component so we have a known starting point. */
        now = get_now((time_t)0);
        for (p1 = dependencies; p1; p1 = p1->next) {
            assert(p1->value);
            switch (!exists(p1->value)) {
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
         * files.
         */
        assert(exists(p->value));
        if (touch(p->value, now)) {
            fprintf(stderr, "Could not update timestamp for %s (cannot "
                "determine dependencies).\n", p->value);
            continue;
        }

        /* The target should not be phony if we've reached this point. */
        assert(!p->phony);

        printf("%s:", p->value);
        old = now; /* The timestamp we've marked each file with. */
        for (p1 = dependencies; p1; p1 = p1->next) {
            now = get_now(old);
            assert(p1->value);
            assert(now > old);
            assert(exists(p1->value));
            assert(get_mtime(p->value) == old);
            touch(p1->value, now);
            if (run(args)) {
                DIE("Error: Failed to build %s after touching %s.\n", p->value,
                    p1->value);
            }
            if (!exists(p->value)) {
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
        if (run(clean)) {
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
