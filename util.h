// Utility functions

#ifndef __util_h__
#define __util_h__

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

// Declare these manually rather than pull in dangerous stdio signatures.
extern int snprintf(char *s, size_t n, const char *format, ...);
extern int vsnprintf(char *s, size_t n, const char *format, va_list ap);

extern void fdprintf(int fd, const char *format, ...);

// Cleanup function called from die
extern void (*at_die)();

extern void die(const char *format, ...) __attribute__((noreturn));
extern int write_str(int fd, const char *s);
extern void wlog(const char *format, ...);

extern int waitall();

#define NOT_IMPLEMENTED(name) \
    die("not implemented: %s at %s:%d", name, __FILE__, __LINE__)

// Join two paths.  The first path must be a canonical absolute path
// (no //,  /./, or /../ entries), but the second may be arbitrary.
// TODO: the result is not canonical, and moreover two unrelated looking
// paths may point to the same file due to symlinks.  Since the snapshot
// data structure implicitly relies on path uniqueness, this is a problem.
// TODO: not thread safe
extern const char *path_join(const char *first, const char *second);

// least_bit_set((1<<11) + (1<<5)) = 1<<5
static inline unsigned least_set_bit(unsigned x)
{
    return ((x ^ (x-1)) >> 1) + 1;
}

#define min(a, b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _b < _a ? _b : _a; })

#define max(a, b) ({ \
    typeof(a) _a = (a); \
    typeof(b) _b = (b); \
    _b > _a ? _b : _a; })

// n must be positive
#define rotate(x, n) ({ \
    typeof(x) _x = (x); \
    int _n = (n); \
    ((_x << _n) | (_x >> (sizeof(typeof(x))/8 - _n))); \
    })

static inline int startswith(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

// Darwin doesn't have memrchr, so make one
#ifndef __linux__
static inline const void *memrchr(const void *p, int c, size_t n)
{
    const char *s = p;
    while (n--)
        if (s[n] == c)
            return s+n;
    return 0;
}
#endif

#endif
