// File descriptor map

#include "fd_map.h"
#include "util.h"
#include "process.h"
#include "real_call.h"
#include "inverse_map.h"

static void check_fd(int fd)
{
    if (!(0 <= fd && fd < MAX_FDS))
        die("fd_map: invalid fd %d", fd);
}

void fd_map_open(int fd, int flags, const struct hash *path_hash)
{
    check_fd(fd);
    struct process *process = lock_process();
    if (process->fds.map[fd])
        die("fd_map_open: reopening open fd %d", fd);
    // Find a free info slot.  Since map and info are the
    // same size, at least one free entry exists.
    int i;
    for (i = 1; ; i++)
        if (!process->fds.info[i].count)
            break;
    process->fds.map[fd] = i;
    process->fds.cloexec[fd] = 0;
    struct fd_info *info = process->fds.info + i;
    info->count = 1;
    info->flags = flags;
    info->path_hash = *path_hash;
    unlock_process();
}

void fd_map_dup2(int fd, int fd2)
{
    if (fd == fd2)
        return;
    check_fd(fd);
    check_fd(fd2);
    struct process *process = lock_process();
    int slot = process->fds.map[fd];
    if (slot) {
        if (process->fds.map[fd2])
            die("fd_map_dup2(%d, %d): %d is open", fd, fd2, fd2);
        process->fds.map[fd2] = slot;
        process->fds.info[slot].count++;
    }
    unlock_process();
}

struct fd_info *fd_map_find(int fd)
{
    check_fd(fd);
    struct process *process = process_info();
    int slot = process->fds.map[fd];
    return slot ? process->fds.info + slot : 0;
}

void fd_map_set_cloexec(int fd, int cloexec)
{
    check_fd(fd);
    struct process *process = lock_process();
    if (process->fds.map[fd])
        process->fds.cloexec[fd] = cloexec;
    unlock_process();
}

void fd_map_close(int fd)
{
    check_fd(fd);
    struct process *process = lock_process();
    int slot = process->fds.map[fd];
    if (slot) {
        process->fds.info[slot].count--;
        process->fds.map[fd] = 0;
    }
    unlock_process();
}

void fd_map_dump()
{
    struct process *process = lock_process();
    fdprintf(STDERR_FILENO, "fd_map dump %d:\n", process->pid);
    int fd;
    for (fd = 0; fd < MAX_FDS; fd++) {
        int slot = process->fds.map[fd];
        if (slot) {
            struct fd_info *info = process->fds.info + slot;
            char buffer[1024];
            if (info->flags & WO_PIPE)
                strcpy(buffer, "<pipe>");
            else
                inverse_hash_string(&info->path_hash, buffer, sizeof(buffer));
            fdprintf(STDERR_FILENO, "  %d: %s, count %d, flags w%d c%d\n",
                fd, buffer, info->count, (info->flags & O_WRONLY) != 0, process->fds.cloexec[fd]);
        }
    }
    unlock_process();
}
