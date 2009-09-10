// Utility functions

#ifndef __util_h__
#define __util_h__

extern void die(const char *format, ...) __attribute__((noreturn));
extern int write_str(int fd, const char *s);
extern void wlog(const char *format, ...);

#define NOT_IMPLEMENTED(name) \
    die("not implemented: %s at %s:%d", name, __FILE__, __LINE__)

// Not thread safe
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

#endif
