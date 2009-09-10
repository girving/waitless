// A very thin wrapper around a cryptographic hash function

// Currently we use the Skein hash function: http://www.skein-hash.info
// Changing the hash function used by waitless should only require changing this file

#ifndef __hash_h__
#define __hash_h__

#include <string.h>
#include "arch.h"

struct hash
{
    uint32_t data[4];
};

static inline int hash_null(const struct hash *p)
{
    int i;
    for (i = 0; i < 4; i++)
        if (p->data[i])
            return 0;
    return 1;
}

static inline int hash_equal(const struct hash *p, const struct hash *q)
{
    return !memcmp(p, q, sizeof(struct hash));
}

// Hash a block of memory
extern void hash_memory(struct hash *hash, const void *p, size_t n);

// Hash a string
extern void hash_string(struct hash *hash, const char *s);

// Hash of the contents of a file descriptor
extern void hash_fd(struct hash *hash, int fd);

#endif
