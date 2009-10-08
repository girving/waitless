// High level actions

#include <errno.h>
#include "action.h"
#include "util.h"
#include "subgraph.h"
#include "snapshot.h"
#include "shared_map.h"
#include "inverse_map.h"
#include "env.h"
#include "real_call.h"
#include "stat_cache.h"
#include "process.h"
#include <stdlib.h>

static void add_parent(struct process *process, const struct hash *parent)
{
    struct parents *parents = &process->parents;

    if (parents->n == MAX_PARENTS)
        die("exceeded MAX_PARENTS = %d", MAX_PARENTS);
    parents->p[parents->n++] = *parent;
}

static void new_node(struct process *process, enum action_type type, const struct hash *data)
{
    struct parents *parents = &process->parents;

    // If there are no parents, skip node generation entirely.  This should
    // happen only from the initial action_execve call in waitless.c.
    if (!parents->n)
        return;

    char buffer[SHOW_HASH_SIZE*parents->n + 3 + SHOW_HASH_SIZE + 1 + SHOW_NODE_SIZE];
    char *p = buffer;
    if (is_verbose()) {
        int i;
        for (i = 0; i < parents->n; i++) {
            p = show_hash(p, 8, parents->p+i);
            *p++ = ' ';
        }
        p = stpcpy(p, "-> ");
    }

    // The new node's name is the hash of its parents' names.
    // We store the new node's name as parents[0] since it will be
    // the first parent of the following node.
    subgraph_node_name(parents->p, parents->p, parents->n);
    parents->n = 1;
    subgraph_new_node(parents->p, type, data);

    if (is_verbose()) {
        p = show_hash(p, 8, parents->p+0);
        p = stpcpy(p, ": ");
        p = show_subgraph_node(p, type, data);
        *p++ = '\n';
        *p = 0;
        write_str(STDERR_FILENO, buffer);
    }
}

/*
 * We treat lstat the same as read except that only the existence or
 * nonexistence of the file is stored (represented as either the all
 * zero hash or the all one hash).
 */
int action_lstat(const char *path)
{
    struct process *process = lock_master_process();

    // Add a stat node to the subgraph
    struct hash path_hash;
    remember_hash_path(&path_hash, path);
    new_node(process, SG_STAT, &path_hash);

    // Check existence and update snapshot
    struct hash exists_hash;
    snapshot_update(&exists_hash, path, &path_hash, 0);
    // No need to check for writers; if the file is being written, it must exist
    shared_map_unlock(&snapshot);

    add_parent(process, &exists_hash);
    unlock_master_process();
    return !hash_is_null(&exists_hash);
}

/*
 * The process is trying to open a file.  We first compute the hash of the path
 * and look it up in the snapshot to see if the file exists.  If it doesn't, we
 * insert a dependency edge from the nonexistence of the file into the process.
 * A "nonexistent file" node in the subgraph is the same as a normal file node
 * except that the hash is all zero (this is very different from the hash of the
 * empty file).
 *
 * If the file does exist, we hash it and depend on the new file node.
 */
int action_open_read(const char *path, const struct hash *path_hash)
{
    struct process *process = lock_master_process();

    // Add a read node to the subgraph
    new_node(process, SG_READ, path_hash);

    // Hash contents and update snapshot
    struct hash contents_hash;
    struct snapshot_entry *entry = snapshot_update(&contents_hash, path, path_hash, 1);
    if (entry->writing)
        die("can't read '%s' while it is being written", path); // TODO: block instead of dying
    entry->read = 1;
    shared_map_unlock(&snapshot);

    add_parent(process, &contents_hash);
    unlock_master_process();
    return !hash_is_null(&contents_hash); 
}

/*
 * action_open_read has already added the subgraph node, so action_close_read
 * only needs to decrement the snapshot reader count and (possibly in the
 * future) check that the file remained constant during the read.
 */
void action_close_read(int fd)
{
    // TODO: Compare contents_hash from snapshot and stat_cache.  In future, we
    // could postpone these checks until the entire process tree completes and
    // check all snapshot entries at the same time.  That has better amortized
    // complexity if the same file is read multiple times.

    // TODO: actually, postponing checks until the end seems like it's clearly
    // the better choice, so let's do that (later).

/*
    shared_map_lock(&snapshot);
    struct snapshot_entry *entry;
    if (!shared_map_lookup(&snapshot, &info->path_hash, (void**)&entry, 0))
        die("action_close_read: shared_map_lookup failed");
    shared_map_unlock(&snapshot);
*/
}

/*
 * action_open_write marks the file as currently being written in the snapshot.
 * The actual subgraph node creation happens below on close.
 */
void action_open_write(const char *path, const struct hash *path_hash)
{
    wlog("action_open_write(%s)", path);
    shared_map_lock(&snapshot);
    struct snapshot_entry *entry;
    if (shared_map_lookup(&snapshot, path_hash, (void**)&entry, 1)) {
        if (entry->read)
            die("can't write '%s': it has already been read", path);
        else if (entry->written)
            die("can't write '%s': it has already been written", path);
        else if (entry->writing)
            die("can't write '%s': it is already being written", path);
    }
    entry->writing = 1;
    shared_map_unlock(&snapshot);
}

void action_close_write(int fd)
{
    // TODO: There is a potential race condition between writing the file and
    // the hash performed in snapshot_update.  I believe the best way to handle
    // this is by intercepting the write system call and hashing the file as it
    // it is written.  We could also consider always doing flock.

    struct fd_info *info = fd_map_find(fd);

    char buffer[1024];
    inverse_hash_string(&info->path_hash, buffer, sizeof(buffer));
    wlog("action_close_write(%s, %d, flags 0x%x)", buffer, fd, info->flags);

    // Hash contents using available file descriptor
    struct hash contents_hash;
    stat_cache_update_fd(&contents_hash, fd, &info->path_hash);

    // Update snapshot
    shared_map_lock(&snapshot);
    struct snapshot_entry *entry;
    if (!shared_map_lookup(&snapshot, &info->path_hash, (void**)&entry, 0))
        die("action_close_write: unexpected missing snapshot entry");
    entry->hash = contents_hash;
    entry->written = 1;
    entry->writing = 0;
    shared_map_unlock(&snapshot);

    // Hash path and contents together
    struct hash data[3];
    data[1] = info->path_hash;
    data[2] = contents_hash;
    remember_hash_memory(data, data+1, 2*sizeof(struct hash));

    // Add a write node to the subgraph
    struct process *process = lock_master_process();
    new_node(process, SG_WRITE, data);
    unlock_master_process();
}

/*
 * Add a fork node to the subgraph add then add an additional parent of
 * either all zeroes or all ones depending on whether we're in child or parent,
 * respectively.
 */
pid_t action_fork(void)
{
    // Analyze open file descriptors
    struct process *process = lock_process();
    int linked = 0, fd;
    for (fd = 0; fd < MAX_FDS; fd++) {
        int slot = process->fds.map[fd];
        if (slot) {
            struct fd_info *info = process->fds.info + slot;
            if (info->flags & WO_PIPE) {
                wlog("fork: fd %d as pipe", fd);
                linked = 1;
            }
            else if (info->flags & O_WRONLY)
                wlog("fork: fd %d open for write", fd);
            else
                wlog("fork: fd %d open for read", fd);
        }
    }
    unlock_process();

    struct hash zero_hash, one_hash;
    memset(&zero_hash, 0, sizeof(struct hash));
    memset(&one_hash, -1, sizeof(struct hash));

    process = lock_master_process();

    // Add a fork node to the subgraph
    new_node(process, SG_FORK, linked ? &zero_hash : &zero_hash);
    wlog("fork: linked %d", linked);

    if (linked)
        unlock_master_process();

    // Actually fork
    pid_t pid = real_fork();
    if (pid < 0)
        die("action_fork: fork failed: %s", strerror(errno));

    // At this point we consider the master lock owned by the child

    if (!pid) {
        struct process *child = new_process_info();
        if (linked) {
            wlog("linking %d to %d", child->pid, process->pid);
            child->master = process->pid;
        }
        else {
            // Inherit from fork node and zero
            add_parent(child, process->parents.p);
            add_parent(child, &zero_hash);
            wlog("fresh process: master 0x%x", child->master);
        }
        // Copy fd_map information to child
        memcpy(&child->fds, &process->fds, sizeof(struct fd_map));
        // Drop fds with close-on-exec set
        int fd;
        for (fd = 0; fd < MAX_FDS; fd++)
            if (child->fds.cloexec[fd])
                child->fds.map[fd] = 0;
        // Unlock both child and master
        unlock_process();
        spin_unlock(&process->lock);
    }
    else if (!linked) {
        // Add a one parent node to the parent
        add_parent(process, &one_hash);
        unlock_master_process();
    }

    return pid;
}

/*
 * action_execve adds an exec node to the subgraph and sets WAITLESS_PARENT
 * to the hash of the arguments.  The first node in the child process will
 * use WAITLESS_PARENT as its first parent node.  Note that WAITLESS_PARENT
 * is intentionally _not_ the same as the exec node; this encodes the idea
 * that child processes depend on their parent processes only through the
 * arguments to execve (and the current directory).
 *
 * In the case of shared spines (due to pipes or other IPC mechanisms) the
 * exec node encodes only the path without argv and envp and WAITLESS_PARENT
 * has the format #id where id is a SYSV shared memory id.  This allows further
 * subgraph nodes from child and parent to be interleaved.  Since in the shared
 * case the child _does_ descend directly from the exec node, an explicit
 * record of argv and envp would be redundant.
 */
void action_execve(const char *path, const char *const argv[], const char *const envp[])
{
    fd_map_dump();
    struct process *process = lock_master_process();
    int linked = process != process_info();
    wlog("exec: linked %d", linked);

    // Pack all the arguments into a single buffer.  The format is
    //     char path[];
    //     uint32_t argc;
    //     char argv[argc][];
    //     uint32_t envc;
    //     char envp[envc][];
    //     char cwd[];
    // with all strings packed together with terminating nulls.
    char data[4096];
    char *p = data;
#define ADD_STR(s) p += strlcpy(p, (s), data+sizeof(data)-p) + 1
    ADD_STR(path);
    // encode argv
    char *cp = p;
    p += sizeof(uint32_t); // skip 4 bytes for len(argv)
    uint32_t i;
    for (i = 0; argv[i]; i++)
        ADD_STR(argv[i]);
    memcpy(cp, &i, sizeof(uint32_t));
    if (!linked) {
        // encode envp, and pwd only in the nonshared case
        char *cp = p;
        cp = p;
        p += sizeof(uint32_t); // skip 4 bytes for len(envp)
        uint32_t count;
        for (i = 0; envp[i]; i++)
            if (!startswith(envp[i], "WAITLESS")) {
                ADD_STR(envp[i]);
                count++;
            }
        memcpy(cp, &count, sizeof(uint32_t));
        if (!real_getcwd(p, data+sizeof(data)-p))
            die("action_execve: getcwd failed: %s", strerror(errno));
    }
    int n = p - data + strlen(p) + 1;
#undef ADD_STR

    // Store exec data and create a corresponding exec node
    struct hash data_hash;
    remember_hash_memory(&data_hash, data, n);
    new_node(process, SG_EXEC, &data_hash);

    // Add the program to the snapshot
    struct hash path_hash, program_hash;
    remember_hash_path(&path_hash, path);
    struct snapshot_entry *entry = snapshot_update(&program_hash, path, &path_hash, 1);
    if (entry->writing)
        die("can't exec '%s' while it is being written", path); // TODO: block instead of dying
    entry->read = 1;
    shared_map_unlock(&snapshot);

    // TODO: Run ldd/otool and hash all shared library dependencies as well
    // TODO: Complain if the program is statically linked
    // TODO: If we decide it's worth it, parse #! lines and hash interpreters
    // too.  That seems easy enough to probably be worth it.

    // If not linked, set parents to the new child values
    if (!linked) {
        process->parents.n = 2; 
        process->parents.p[0] = data_hash;
        process->parents.p[1] = program_hash;
    }

    unlock_master_process();
}

void action_exit(int status)
{
    // Flush all open streams
    fflush(0);

    // Close all open file descriptors
    struct process *process = lock_process();
    int fd;
    for (fd = 1; fd < MAX_FDS; fd++)
        if (process->fds.map[fd]) {
            // Call raw close to trigger action logic
            extern int close(int fd);
            unlock_process(); // TODO: minor race condition here
            close(fd);
            process = lock_process();
        }
    unlock_process();

    process = lock_master_process();

    // Add an exit(status) node to the subgraph
    struct hash data;
    memset(&data, 0, sizeof(data));
    data.data[0] = status;
    new_node(process, SG_EXIT, &data);

    unlock_master_process();
}
