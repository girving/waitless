// Atomic operations and spinlocks

// For now we support only gcc x86.  Fixing that is mostly a job of looking
// around at other spinlock code in search of an appropriate license.

#ifndef __spinlock_h__
#define __spinlock_h__

#include "util.h"

static inline int xchg(volatile int *m, int x)
{
    int r;
    __asm__ (
        "xchg%z0 %2, %0"
        : "=g"(*m), "=r"(r)
        : "1"(x));
    return r;
}

typedef struct {
    // 1 is locked, 0 is unlocked
    int lock;
} spinlock_t;

static inline void spin_lock(spinlock_t *s)
{
    // Repeatedly set lock to 1 until we see that it used to be zero,
    // meaning that we now own the lock.
    unsigned int counter = 0;
    while (xchg(&s->lock, 1)) {
        if (counter++ == 50000000)
            die("spun out 0x%x", s);
    }
    //wlog("lock   0x%x", s);
}

static inline void spin_unlock(spinlock_t *s)
{
    //wlog("unlock 0x%x", s);
    s->lock = 0;
}

#endif
