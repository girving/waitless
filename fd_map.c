// File descriptor map

#include "fd_map.h"
#include "util.h"

struct fd_info fd_map[MAX_FDS];

void fd_map_open(int fd, int flags)
{
    NOT_IMPLEMENTED("fd_map_open");
}

struct fd_info *fd_map_find(int fd)
{
    NOT_IMPLEMENTED("fd_map_find");
}

void fd_map_close(struct fd_info *info)
{
    NOT_IMPLEMENTED("fd_map_close");
}
