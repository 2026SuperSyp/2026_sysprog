#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_KEYS    256
#define MAX_KEY_LEN 96

static char seen[MAX_KEYS][MAX_KEY_LEN];
static int  n_seen;

static int already_seen(const char *key)
{
    for (int i = 0; i < n_seen; i++)
        if (strncmp(seen[i], key, MAX_KEY_LEN - 1) == 0)
            return 1;
    return 0;
}

void log_warn_once(const char *key, const char *fmt, ...)
{
    if (already_seen(key))
        return;
    if (n_seen < MAX_KEYS) {
        strncpy(seen[n_seen], key, MAX_KEY_LEN - 1);
        seen[n_seen][MAX_KEY_LEN - 1] = '\0';
        n_seen++;
    }

    va_list ap;
    va_start(ap, fmt);
    fputs("경고: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

int log_warning_count(void)
{
    return n_seen;
}
