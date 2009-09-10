// File descriptor map

#ifndef __fd_map_h__
#define __fd_map_h__

#include "hash.h"

/*
 * A map from open file descriptors to information about the files.
 */

struct fd_info
{
    int active;
    int flags; // flags passed to open
    struct hash path_hash;
};

#define MAX_FDS 256

extern struct fd_info fd_map[MAX_FDS];

extern void fd_map_open(int fd, int flags);
extern struct fd_info *fd_map_find(int fd);
extern void fd_map_close(struct fd_info *info);

#endif
