// Endian-conversion routines

#ifndef __endian_h__
#define __endian_h__

#include <string.h>
#include "arch.h"

// Pull in htole64 and le64toh
#if defined(__APPLE__)
    // sys/endian.h is crippled, so roll our own (very easily)
#   define htole64(x) ((uint64_t)(x))
#   define letoh64(x) ((uint64_t)(x))
#else
#   include <sys/endian.h> 
#endif

// Copy a block of memory, switching from native to 64-bit little endian
static inline void memcpy_htole64(uint8_t *__restrict dst, const uint64_t *__restrict src, size_t n)
{
    if (htole64(1) == 1) // Little endian: do the memcpy
        memcpy(dst, src, n);
    else { // Big endian: do the simple slow thing (from skein_port.h)
        ssize_t i;
        for (i = 0; i < n; i++)
            dst[i] = (uint8_t)(src[i>>3] >> (8*(i&7)));
    }
}

// Copy a block of memory, switching from 64-bit little endian to native.
// n must be a multiple of 8.
static inline void memcpy_letoh64(uint64_t *__restrict dst, const uint8_t *__restrict src, size_t n)
{
    memcpy(dst, src, n);
    if (letoh64(1) != 1) { // Big endian: byteswap the destination in place
        ssize_t i;
        for (i = 0; i < n; i++)
            dst[i] = letoh64(dst[i]);
    }
}

#endif
