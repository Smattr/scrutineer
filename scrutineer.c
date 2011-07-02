#include <utime.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_CLEAN_TARGET "clean"

typedef struct list {
    char *value;
    struct list *next;
} list_t;

/* A list of potential dependencies for each target. */
list_t *components = NULL;

/* A list of targets to assess. */
list_t *targets = NULL;

/* Sets the modified time of a file. Returns 0 on success or -1 on failure. The
 * reason for this function is to abstract calls to utime so we can replace it
 * with something else later if necessary.
 */
int touch(const char *path, const struct utimbuf *timestamp) {
    return utime(path, timestamp);
}

int main(int argc, char **argv) {
    return 0;
}
