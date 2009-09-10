// High level actions

#include "action.h"
#include "util.h"

int action_lstat(const char *path)
{
    NOT_IMPLEMENTED("action_lstat");
}

int action_open_read(const char *path)
{
    NOT_IMPLEMENTED("action_open_read");
}

void action_close_read(struct fd_info *info)
{
    NOT_IMPLEMENTED("action_close_read");
}

void action_open_write(const char *path)
{
    NOT_IMPLEMENTED("action_open_write");
}

void action_close_write(struct fd_info *info)
{
    NOT_IMPLEMENTED("action_close_write");
}

void action_execve(const char *path, char *const argv[], char *const envp[])
{
    NOT_IMPLEMENTED("action_execve");
}
