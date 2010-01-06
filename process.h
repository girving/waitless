// Per-process information

#ifndef __process_h__
#define __process_h__

/*
 * The per-process state in waitless is stored in an mmapped file to support
 * nontrivial logic across forks and execs.  Like the snapshot, this file is
 * unique to the current invocation of waitless.
 */

#include "hash.h"
#include "arch.h"
#include "fd_map.h"
#include "spinlock.h"

// TODO: Teach fd_map about file descriptors open at startup, such as 0,1,2.
// TODO: Consider padding struct process out to a page boundary to avoid cache
// contention across different processes.

// Maximum number of parents of a single node (increase as needed)
#define MAX_PARENTS 2

struct parents
{
    // List of subgraph nodes that the next process node should depend on.
    int n;
    struct hash p[MAX_PARENTS];
};

struct process
{
    pid_t pid;
    spinlock_t lock;

    // Flags for tweaking process behavior
    int flags;

    // If processes are linked by a pipe, subgraph nodes from both processes
    // are interleaved into the process info of the master.
    pid_t master;

    // Meaningful only if master is zero
    struct parents parents;

    // Information about open file descriptors
    struct fd_map fds;
};

// Make a fresh process map and store its path in WAITLESS_PROCESS.
extern void make_fresh_process_map();

// Create a fresh process info entry for the current process and return it.
// The entry is returned locked, so the caller must unlock it after
// initialization.
extern struct process *new_process_info();

// Find an existing entry for any process, or die if none exists.
extern struct process *find_process_info(pid_t pid) __attribute__ ((pure));

// Find an existing entry for the current process.
extern struct process *process_info() __attribute__ ((pure));

// Locking version of process_info
extern struct process *lock_process();
extern void unlock_process();

// Same as lock_process but returns the master process if processes are linked.
extern struct process *lock_master_process();
extern void unlock_master_process();

// Kill all registered processes
extern void killall();

#endif
