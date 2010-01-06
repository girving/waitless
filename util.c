// Utility functions

#include <stdarg.h>
#include <string.h>
#include <execinfo.h>
#include "util.h"
#include "real_call.h"

void fdprintf(int fd, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char buffer[1024];
    int n = vsnprintf(buffer, sizeof(buffer), format, ap);
    write(STDERR_FILENO, buffer, min(n, sizeof(buffer)-1));
    va_end(ap);
}

void write_backtrace()
{
    void *stack[20];
    int count = backtrace(stack, 20);
    char **strings = backtrace_symbols(stack, count);
    fdprintf(STDERR_FILENO, "stack trace %d:\n", getpid());
    int i;
    for (i = 0; i < count; i++)
        fdprintf(STDERR_FILENO, "  %s\n", strings[i]);
    void free(void*);
    free(strings);
}

void (*at_die)();

void die(const char *format, ...)
{
    write_backtrace();

    char buffer[1024], *p = buffer;
    p += snprintf(p, p-buffer+sizeof(buffer)-1, "fatal %d: ", getpid());
    va_list ap;
    va_start(ap, format);
    p += vsnprintf(p, p-buffer+sizeof(buffer)-1, format, ap);
    *p++ = '\n';
    write(STDERR_FILENO, buffer, p - buffer);

    if (at_die)
        at_die();
    real__exit(1);
}

void wlog(const char *format, ...)
{
    char buffer[1024], *p = buffer;
    p += snprintf(p, p-buffer+sizeof(buffer)-1, "log %d: ", getpid());
    va_list ap;
    va_start(ap, format);
    p += vsnprintf(p, p-buffer+sizeof(buffer)-1, format, ap);
    va_end(ap);
    *p++ = '\n';
    write(STDERR_FILENO, buffer, p - buffer);
}

int waitall()
{
    int ret = 0, status;
    while (real_wait(&status) > 0) {
        if (!ret) {
            if (WIFEXITED(status))
                ret = WEXITSTATUS(status);
            else
                ret = 1;
        }
    }
    return ret;
}

int write_str(int fd, const char *s)
{
    return write(fd, s, strlen(s));
}

const char *path_join(const char *first, const char *second)
{
    if (second[0] == '/')
        return second;
    else if (second[0] == '.' && !second[1])
        return first;
    static char result[PATH_MAX];
    int n1 = strlen(first), n2 = strlen(second);
    if (first[0] != '/' || first[n1] == '/')
        die("path_join: first path must be absolute, not %s", first);
    if (n1 + n2 + 2 > PATH_MAX)
        die("path_join failed: %d + %d + 2 > %d", n1, n2, PATH_MAX);

    // Strip ./ from second and cancel ../ prefixes with components of first
    for (;;) {
        if (second[0] == '/') {
            // remove a redundant / character
            second++; 
            n2--;
        }
        else if (second[0] == '.') {
            if (second[1] == '/') {
                // strip ./ prefix
                second += 2;
                n2 -= 2;
            }
            else if (second[2] == '.') {
                if (second[3] == '/') {
                    // cancel ../ with last component of first
                    n1 = max((const char*)memrchr(first, '/', n1) - first, 1);
                    second += 3;
                    n2 -= 3;
                }
                else
                    break;
            }
            else
                break;
        }
        else
            break;
    }

    memcpy(result, first, n1);
    result[n1] = '/';
    memcpy(result+n1+1, second, n2);
    result[n1+1+n2] = 0;
    return result;
}
