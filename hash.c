// A very thin wrapper around a cryptographic hash function

#include <string.h>
#include "skein.h"
#include "hash.h"
#include "util.h"
#include "real_call.h"

static Skein_512_Ctxt_t context;

void hash_memory(struct hash *hash, const void *p, size_t n)
{
    Skein_512_Init(&context, 8*sizeof(hash));
    Skein_512_Update(&context, p, n);
    Skein_512_Final(&context, (uint8_t*)hash);
}

void hash_string(struct hash *hash, const char *s)
{
    hash_memory(hash, s, strlen(s));
}

void hash_fd(struct hash *hash, int fd)
{
    Skein_512_Init(&context, 8*sizeof(hash));
    char buffer[16*1024];
    for (;;) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len < 0)
            die("read failed in hash_fd");
        else if(len == 0)
            break;
        Skein_512_Update(&context, (uint8_t*)buffer, len);
    }
    Skein_512_Final(&context, (uint8_t*)hash);
}
