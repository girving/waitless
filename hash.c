// A very thin wrapper around a cryptographic hash function

#include <string.h>
#include "skein.h"
#include "hash.h"
#include "util.h"
#include "real_call.h"
#include <errno.h>

static Skein_512_Ctxt_t context;

void hash_memory(struct hash *hash, const void *p, size_t n)
{
    Skein_512_Init(&context, 8*sizeof(struct hash));
    Skein_512_Update(&context, p, n);
    Skein_512_Final(&context, (uint8_t*)hash);
}

void hash_string(struct hash *hash, const char *s)
{
    hash_memory(hash, s, strlen(s));
}

void hash_fd(struct hash *hash, int fd)
{
    Skein_512_Init(&context, 8*sizeof(struct hash));
    char buffer[16*1024];
    for (;;) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len < 0)
            die("read failed in hash_fd: %s", strerror(errno));
        else if(len == 0)
            break;
        Skein_512_Update(&context, (uint8_t*)buffer, len);
    }
    Skein_512_Final(&context, (uint8_t*)hash);
}

static inline char show_nibble(unsigned char n)
{
    return n < 10 ? '0' + n : 'a' + n - 10;
}

char *show_hash(char *s, int n, const struct hash *hash)
{
    n = min(n, SHOW_HASH_SIZE);
    char *p = s;
    int i;
    for (i = 0; i < n/2; i++) {
        unsigned char b = ((const char*)hash)[i];
        *p++ = show_nibble(b >> 4);
        *p++ = show_nibble(b & 0xf);
    }
    if (!(n & 1)) // unset the last character for odd lengths
        p--;
    *p = 0;
    return p;
}

const char *read_hash(struct hash *hash, const char *s)
{
#define READ_NIBBLE(e) ({ \
    char c = (e), r; \
    if ('0' <= c && c <= '9') r = c - '0'; \
    else if ('a' <= c && c <= 'f') r = c - 'a' + 10; \
    else if ('A' <= c && c <= 'F') r = c - 'A' + 10; \
    else goto error; \
    r; \
    })

    int i;
    const char *p = s;
    for (i = 0; i < sizeof(struct hash); i++) {
        int hi = READ_NIBBLE(*p++),
            lo = READ_NIBBLE(*p++);
        ((char*)hash)[i] = (hi << 4) | lo;
    }

    return p;
error:
    die("read_hash: invalid hash string '%s'", s);
}
