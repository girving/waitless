// Utility functions

#include <stdarg.h>
#include <string.h>
#include "util.h"
#include "real_call.h"

// Declare this manually rather than pull in dangerous stdio signatures.
extern int vsnprintf(char *s, size_t n, const char *format, va_list ap);

static void vfdprintf(int fd, const char *prefix, const char *format, va_list ap, const char *suffix)
{
    char buffer[1024], *p = buffer;
    p = stpcpy(p, prefix); 
    int suffix_len = strlen(suffix);
    int n = p - buffer + sizeof(buffer) - suffix_len + 1;
    p += min(n, vsnprintf(p, n, format, ap));
    memcpy(p, suffix, suffix_len); 
    write(STDERR_FILENO, buffer, p - buffer + suffix_len);
}

void die(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfdprintf(STDERR_FILENO, "fatal: ", format, ap, "\n");
    real__exit(1);
}

void wlog(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfdprintf(STDERR_FILENO, "log: ", format, ap, "\n");
    va_end(ap);
}

int write_str(int fd, const char *s)
{
    return write(fd, s, strlen(s));
}

const char *path_join(const char *first, const char *second)
{
    static char result[PATH_MAX];
    int n = strlen(first);
    memcpy(result, first, n);
    result[n] = '/';
    strcpy(result+n+1, second);
    return result;
}
