// File descriptor map

#ifndef __fd_map_h__
#define __fd_map_h__

#include "hash.h"

// Special flags for fd_info
#define WO_PIPE    0x10000000 // came from pipe()
#define WO_FOPEN   0x20000000 // came from fopen()

#define MAX_FDS 256

struct fd_info
{
    int count; // 0 is closed, 1 is open, > 1 is open and dup'ed
    int flags; // flags passed to open plus a few of our own
    struct hash path_hash;
};

struct fd_map
{
    int map[MAX_FDS];
    int cloexec[MAX_FDS]; // close-on-exec flags
    struct fd_info info[MAX_FDS];
};

extern void fd_map_open(int fd, int flags, const struct hash *path_hash);

extern void fd_map_dup2(int fd, int fd2);

// Returns null if the file descriptor is inactive
extern struct fd_info *fd_map_find(int fd);

extern void fd_map_set_cloexec(int fd, int cloexec);

extern void fd_map_close(int fd);

extern void fd_map_dump();

#endif
