// High level actions

#ifndef __action_h__
#define __action_h__

#include "fd_map.h"
#include <sys/types.h>

/*
 * This file is an abstracted model of the system calls of a process.
 * The low level system call stubs in stubs.c call these functions to
 * express what's going on.  Actions are simpler than system calls in
 * the following ways:
 *
 * 1. There are many, many, many fewer of them.
 *
 * 2. Actions return 1 for success and 0 for failure.  There is usually only
 *    one type of failure, or zero if all failures are invisible to the caller.
 *
 * 3. All actions are allowed to block or never return if they so choose.
 *
 * The descriptions of the declarations in this header are intentionally brief;
 * they describe how one should think about what the process is doing, not what
 * waitless will have to do behind the scenes in order to make everything play
 * nicely.  See action.c for the latter.
 */

// Check whether a file exists.
int action_lstat(const char *path);

// Start reading a file.  Returns false if the file doesn't exist.
int action_open_read(const char *path, const struct hash *path_hash);

// Finish reading a file.  If the file failed to open, info is NULL.
void action_close_read(int fd);

// Start writing a file.
void action_open_write(const char *path, const struct hash *path_hash);

// Finish writing a file.  If the file failed to open, info is NULL.
void action_close_write(int fd);

// Fork.  Unlike most action calls, action_fork calls real_fork internally.
pid_t action_fork(void);

// Exec.
void action_execve(const char *path, const char *const argv[], const char *const envp[]);

// Exit.
void action_exit(int status);

#endif
