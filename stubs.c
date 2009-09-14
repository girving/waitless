// Stubs to intercept system calls and insert trickery

#include <errno.h>
#include "util.h"
#include "fd_map.h"
#include "action.h"
#include "real_call.h"

/*
 * READ THIS FIRST:
 * 
 * In order to preserve our sanity, this file contains little actual logic.
 * Instead, it maps the vast complexity zoo that is system calls into the
 * much simpler model in action.c.  Start in action.c if you're reading this
 * for the first time.
 *
 * Now for a few comments:
 */

/*
 * Why LD_PRELOAD:
 *
 * We want to intercept various system calls and insert fancy logic.  On Linux,
 * this can be accomplished by either ptrace or LD_PRELOAD.  ptrace is far
 * more robust by virtue of being kernel level and having exact control over
 * every system call.  Unfortunately, it is quite slow, both because every
 * tracer/tracee interaction is a system call and because there is no way to
 * ptrace a subset of system calls.
 *
 * LD_PRELOAD, on the other hand, is fast but fragile:
 *
 * 1. LD_PRELOAD can only intercept dynamically linked function calls.  In
 *    particular, it does not work on processes which perform system calls
 *    directly via interrupts/traps or on processes statically linked calls
 *    to libc.  This has at least one important special case:
 *
 * 2. LD_PRELOAD does not detect system calls originating from glibc due to
 *    (1).  Therefore, we intercept the C library functions as well.  Hopefully
 *    other languages aren't doing this as well.
 *
 *    When we intercept libc functions, we also cannot assume that the
 *    underlying system call _won't_ be called through a dynamic link.
 *    Therefore, care is required to avoid executing the wrapper logic twice.
 *
 * 3. Our stubs are loaded by ld.so, so any system calls made by ld.so itself
 *    will be missed.  Instead, we compute library dependency information
 *    immediately _before_ execve using ldd (or the equivalent).
 *
 * Since we have practical workarounds for (2) and (3), and can provisionally
 * hope that (1) is rare, we'll go with LD_PRELOAD.
 *
 * TODO: Provide a ptrace mode for when we want to really know.  The
 * strace source code is BSD licensed, so we can probably borrow its ptrace
 * logic.  Alternatively, we could parse 'strace -f' directly.
 */

/*
 * General notes about LD_PRELOAD:
 *
 * 1. Once we intercept a function call, we (may) need to forward along to
 *    the real version.  There are at least three methods:
 *
 *    a. syscall (for system calls only)
 *    b. dlsym(RTLD_NEXT, ...)
 *    c. ld --wrap: http://stackoverflow.com/questions/998464
 *
 *    (a) is unfriendly because it breaks other preloaded libraries.
 *    TODO: We use (b) now, but should probably witch to (c) for speed.
 *
 * 2. TODO: Thread safety!  All the functions we're wrapping are thread safe,
 *    so clearly we need to be thread safe too.
 *
 * 3. For purposes of computing dependencies, we consider each system call to
 *    either succeed for fail.  The particular kind of failure is not recorded.
 *
 *    TODO: That is completely wrong.  For each system call, we need to divide
 *    the failure modes into "normal failures" and "transient failures".  If
 *    gcc fails to read a file because of some weird NFS problem or for lack of
 *    memory, we need to avoid accidentally caching that as a deterministic
 *    failure.
 *
 *    We'll need to go through all the calls to real_* and make sure
 *    each one treats each failure appropriately.
 */

/*
 * Which system calls we intercept:
 *
 * The space of system calls is vast and we do not want to get into the
 * process jail business.  Therefore, we pay attention to only those we
 * think are important, and all others as sources of nondeterminism.
 *
 * TODO: if we get ptrace mode (either natively or by parsing strace)
 * we'll be able to check these assumptions.
 *
 * There are two classes of important system calls:
 * 
 * 1. File IO: We track any system call that touches a filesystem path in
 *    preparation for (or conclusion of) reading and writing:
 *
 *        open, creat, close
 *        stat, lstat, access
 *        chdir, rename, truncate
 *
 *    TODO: ...plus their libc equivalents:
 *
 *        fopen, fdopen, freopen, fclose, fcloseall
 *
 *    We _do not_ track calls which read or write data through file
 *    descriptors.  Instead, we imagine that when a process opens a file
 *    for reading it instantly learns the entire contents of the file.
 *    Contravariantly, we imagine that when a process closes a file it
 *    instantly rewrites the entire file.  These conservative assumptions
 *    avoid the need to track each individual read and write.
 *
 *    TODO: For processes that read or write files in pleasant linear fashion,
 *    it is possible to feed the data streams through our hash directly,
 *    saving a huge amount of extra IO.  This optimization is relatively
 *    straightforward; it requires adding stubs for
 *
 *        read, readv, pread: thread input through hash
 *        write, writev, pwrite: thread output through hash
 *        lseek, llseek, ftruncate: fall back to pre/post hashing
 *
 *    TODO: Tracking write will also let us store and reply the stdout/stderr
 *    stream for processes.
 *
 *    Our notion of instantaneous information flow on open depends on
 *    files not changing between open and close.  We don't need to worry about
 *    other processes under waitless, but we do have to watch out for unrelated
 *    (enemy) processes.  Therefore, we perform an additional fstat immediately
 *    before close and compare it to the stat_cache on files open for read.
 *    There is no need for a similar check on files open for write since an
 *    enemy process writing our files at the same time is no worse than a
 *    process rewriting a file _after_ we're done with it.  We'll detect the
 *    problem if we later read the same file, but otherwise there is nothing to
 *    do.  The snapshot guarantee is that all processes under waitless see the
 *    same version of every file, NOT that these versions are available to the
 *    user at any later time.
 *
 *    If waitless is run on top of another build system, the child system will
 *    probably try to use stat or lstat to determine whether files need to be
 *    rebuilt.  All information received through stat or lstat is considered
 *    nondeterminism _except_ the one bit of whether the file exists or not.
 *    This includes the length of the file.  TODO: Think about symlinks.
 *
 * 2. Process management: fork, execve, wait, and exit.  We do not
 *    track clone; threads are considered the same processes (TODO: thread
 *    safety).  The complete list is
 *
 *        fork, vfork
 *        execve
 *        wait, wait3, wait4, waitpid, waitid
 *        exit
 *
 *    TODO: ...plus their libc equivalents:
 *
 *        system, execl, execle, execlp, execv, execvp, execvP
 *
 * We do not track the following classes of system calls.  The many TODOs are
 * listed in vaguely reverse order of how ridiculous they are; if you want to
 * fix some of these, start looking at the end.
 *
 * 1. Network: The network is treated as a huge source of nondeterminism.
 *    Inputs and outputs which occur over the network are not tracked.  This
 *    model works well for processes like distcc which bundle up local inputs,
 *    send them across the network for some deterministic computation, and
 *    write the outputs to disk back on the local machine.
 *
 * 2. Signals: More nondeterminism, but sometimes harmless nondeterminism.  In
 *    particular, we consider kill equivalent to a process dying spontaneously,
 *    which is implicitly tracked by the absense of _exit.
 *
 * 3. Interprocess communication: Same as the network.
 *
 * 4. TODO: There are a variety of linux-specific system calls that we should
 *    be tracking but aren't.  These include:
 *
 *    fchmodat, newfstatat, unlinkat, fchownat, openat, renameat,
 *    symlinkat, readlinkat, linkat, faccessat, mkdirat
 *
 * 5. TODO: The 32/64-bit interim system calls:
 *
 *    stat64, lstat64, fstat64, truncate64, getdents64
 *
 * 6. TODO: Pipes: We will eventually handle pipes and treat their asynchronous
 *    behavior of pipes as a source of nondeterminism.  There are three levels
 *    of support for pipes:
 *
 *    a. Die.  Pipes are detected and explicitly not allowed.
 *    b. Treat them as magic.  If two processes are connected with a pipe, they
 *       are assumed to share all information until the pipe is closed.
 *    c. Full.  Data flowing through pipes is hashed and possibly cached for
 *       full dependency output.
 *
 *    All of these levels will require stubs for
 *
 *        pipe, dup, dup2, fcntl
 *
 * 7. Ownership: chown, fchown
 *
 * 8. TODO: Directory related system calls:
 *
 *        mkdir, rmdir, readdir, getdents
 *
 * 9. TODO: Symlinks.  We currently treat stat and lstat as if they were
 *     the same, and ignore symlink specific system calls:
 *
 *         symlink, readlink
 *
 * 10. TODO: Hard links: unlink, link
 *
 * 11. TODO: Permissions: chmod, fchmod
 *
 * 12. TODO: mmap, munmap.  These are important because data can be written to
 *     an mmap'ed region _after_ a file is closed.
 *
 * 13. TODO: temporary file creation:
 *
 *         mkdtemp, mkstemps, mkstemp, mktemp
 */

static const int required_open_flags = O_CREAT | O_TRUNC;
static const int disallowed_open_flags = O_APPEND | O_EXCL | O_EVTONLY;

static void bad_open_flags(int flags) __attribute__((noreturn));
static void bad_open_flags(int flags)
{
    const char *s = NULL;
    if (flags & O_APPEND)   s = "O_APPEND is currently disallowed for open";
    if (!(flags & O_CREAT)) s = "O_CREAT is currently required for open";
    if (!(flags & O_TRUNC)) s = "O_TRUNC is currently required for open";
    if (flags & O_EXCL)     s = "O_EXCL is currently disallowed for open";
    if (flags & O_EVTONLY)  s = "O_EVTONLY is currently disallowed for open";
    die(s);
}

// Set visibility to default for all the stubs.
#pragma GCC visibility push(default)

/*
 * open takes either two or three arguments (mode is only needed with O_CREAT),
 * but it is safe to always take three and forward them along thanks to the
 * structure of essentially all calling conventions.
 */
int open(const char *path, int flags, mode_t mode)
{
    int inside_libc_save = inside_libc;
    if (!inside_libc_save) {
        // TODO: deal with O_NOFOLLOW and O_SYMLINK
        if ((flags & (required_open_flags | disallowed_open_flags)) ^ required_open_flags)
            bad_open_flags(flags);
        if (flags & O_RDWR)
            NOT_IMPLEMENTED("O_RDWR");
        if (flags & O_WRONLY)
            action_open_write(path);
        else { // O_RDONLY
            if (!action_open_read(path)) {
                // File does not exist, no need to call open.
                errno = ENOENT;
                return -1;
            }
        }
    }

    int fd = real_open(path, flags, mode);

    if (!inside_libc_save) {
        if (fd >= MAX_FDS)
            die("got fd %d > %d", fd, MAX_FDS);
        else if (fd > 0)
            fd_map_open(fd, flags);
        else { // fd == -1
            if (flags & O_WRONLY)
                action_close_write(NULL);
            else
                action_close_read(NULL);
        }
    }
    return fd;
}

int creat(const char *path, mode_t mode)
{
    return open(path, O_CREAT | O_TRUNC | O_WRONLY, mode);
}

FILE *fopen(const char *path, const char *mode)
{
    if (mode[1] == '+' || mode[0] == 'a')
        die("unsupported fopen mode '%s'", mode);
    int flags = mode[0] == 'w' ? O_WRONLY : O_RDONLY;
    if (flags & O_WRONLY)
        action_open_write(path);
    else { // O_RDONLY
        if (!action_open_read(path)) {
            // File does not exist, no need to call fopen.
            errno = ENOENT;
            return NULL;
        }
    }

    FILE *file = real_fopen(path, mode);

    if (!file) {
        if (flags & O_WRONLY)
            action_close_write(NULL);
        else
            action_close_read(NULL);
    }
    int fd = fileno(file);
    if (fd >= MAX_FDS)
        die("got fd %d > %d", fd, MAX_FDS);
    else
        fd_map_open(fd, flags);
    return file;
}

FILE *fdopen(int fd, const char *mode)
{
    NOT_IMPLEMENTED("fdopen");
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    NOT_IMPLEMENTED("freopen");
}

/*
 * The close function is a cool example of contravariance.  If the file is open
 * for read, we call action_close_read before the close in order to use fstat
 * fstat to verify that the file hasn't changed.  In the case of write, we call
 * close before action_close_write so that errors detected on close manifest as
 * write errors.
 *
 * TODO: There is an extremely dangerous race condition when writing a file.
 * We currently hash the output file after it is fully written, which means
 * that another process could modify the file between close and hash.  I believe
 * the only reliable way to avoid this race condition is to intercept the write
 * stream and compute the hash as the data is written out.  Happily, that's
 * something we want to do anyway.
 *
 * For context: the way it should work is that we should compute the hash value
 * that we would have gotten in isolation regardless of when other processes
 * try to stomp on our files.  That way, even if we get the modification times
 * wrong, blowing away the stat_cache and rerunning will detect the change and
 * rebuild the necessary files.
 */
int close(int fd)
{
    struct fd_info *info;
    int inside_libc_save = inside_libc;
    if (!inside_libc_save) {
        info = fd_map_find(fd);
        if (!info)
            die("bad file descriptor in close");
        if (!(info->flags & O_WRONLY)) // O_RDONLY
            action_close_read(info);
    }

    int ret = real_close(fd);

    if (!inside_libc_save) {
        if (ret < 0)
            die("error on close");
        if (info->flags & O_WRONLY)
            action_close_write(info);
        fd_map_close(info);
    }
    return ret;
}

int fclose(FILE *stream)
{
    struct fd_info *info = fd_map_find(fileno(stream));
    if (!info)
        die("bad FILE* in fclose");
    if (!(info->flags & O_WRONLY)) // O_RDONLY
        action_close_read(info);

    int ret = real_fclose(stream);

    if (ret)
        die("error on fclose");
    if (info->flags & O_WRONLY)
        action_close_write(info);
    fd_map_close(info);
    return 0;
}

int fcloseall()
{
    NOT_IMPLEMENTED("fcloseall");
}

int fstat(int fd, struct stat *buf)
{
    NOT_IMPLEMENTED("fstat");
}

int lstat(const char *path, struct stat *buf)
{
    if (!action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }
    // TODO: Coalesce this lstat with the one in action_read
    return real_lstat(path, buf);
}

int stat(const char *path, struct stat *buf)
{
    // TODO: Don't pretend that lstat and stat are the same.  stat should be
    // modeled as the sequence of lstats that it is.
    if (!action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }
    // TODO: Coalesce stat calls
    return real_stat(path, buf);
}

int access(const char *path, int amode)
{
    // TODO: make this the same as stat (i.e., not lstat)
    if (!action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }
    return real_access(path, amode);
}

int chdir(const char *path)
{
    NOT_IMPLEMENTED("chdir");
}

int fchdir(int fd)
{
    NOT_IMPLEMENTED("fchdir");
}

int rename(const char *old, const char *new)
{
    NOT_IMPLEMENTED("rename");
}

int truncate(const char *path, off_t len)
{
    NOT_IMPLEMENTED("truncate");
}

pid_t fork(void)
{
    NOT_IMPLEMENTED("fork");
    return real_fork();
}

pid_t vfork(void)
{
    NOT_IMPLEMENTED("vfork");
    return real_vfork();
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    action_execve(path, argv, envp);
    return real_execve(path, argv, envp);
}

// TODO: Add stubs for system, execl, execle, execlp, execv, execvp, execvP

pid_t wait(int *status)
{
    NOT_IMPLEMENTED("wait"); 
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    NOT_IMPLEMENTED("waitpid"); 
}

#ifdef __linux__
int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) 
{
    NOT_IMPLEMENTED("waitid"); 
}
#endif

pid_t wait3(int *status, int options, struct rusage *rusage)
{
    NOT_IMPLEMENTED("wait3"); 
}

pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
    NOT_IMPLEMENTED("wait4"); 
}

void _exit(int status)
{
    NOT_IMPLEMENTED("_exit");
}

void _Exit(int status)
{
    _exit(status);
}

void exit(int status)
{
    NOT_IMPLEMENTED("exit");
}

#ifdef __APPLE__

/*
 * For possibly legitimate backwards compatibility reasons, Apple decided they
 * needed to make extra versions of functions with names like open$UNIX2003.
 * Intercept these too.  Here are some unsatisfying details:
 *
 * http://developer.apple.com/mac/library/releasenotes/Darwin/SymbolVariantsRelNotes
 */

#define DARWIN_ALIAS(name) __asm("_" #name "$UNIX2003")

int open_darwin(const char *path, int flags, mode_t mode) DARWIN_ALIAS(open);
int open_darwin(const char *path, int flags, mode_t mode)
{
    NOT_IMPLEMENTED("open_darwin");
}

FILE *fopen_darwin(const char *path, const char *mode) DARWIN_ALIAS(fopen);
FILE *fopen_darwin(const char *path, const char *mode)
{
    NOT_IMPLEMENTED("fopen_darwin");
}

#pragma GCC visibility pop

#endif
