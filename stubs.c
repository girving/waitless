// Stubs to intercept system calls and insert trickery

#include <errno.h>
#include "util.h"
#include "fd_map.h"
#include "action.h"
#include "real_call.h"
#include "inverse_map.h"
#include "search_path.h"

/*
 * READ THIS FIRST:
 *
 * For simplicity, this file contains little actual logic.  Instead, it maps
 * the vast complexity zoo that is system calls into the much simpler model
 * in action.c.  Start in action.c if you're reading this for the first time.
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
 *
 * 4. errno is not necessarily preserved by action_ or other calls, which might
 *    result in weird errno values when system calls fail.
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
 *    plus their libc equivalents:
 *
 *        fopen, fdopen, freopen, fclose, fcloseall
 *
 *    We _do not_ track calls which read or write data through file
 *    descriptors (or use fstat to grab information about them)  Instead, we
 *    imagine that when a process opens a file for reading it instantly learns
 *    the entire contents of the file.  Contravariantly, we imagine that when
 *    a process closes a file it instantly rewrites the entire file.  These
 *    conservative assumptions avoid the need to track each individual read
 *    and write.
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
 *    user at any later time.  TODO: make sure we do the final fstat check
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
 *    In order to do (b), we need the ability to share the "parents" variable
 *    in action.c between two kernel processes in order to treat them as the
 *    same "waitless" process.  Probably the best way is a shared mmap'ed
 *    region, or maybe SYSV shared memory.  We could communicate the information
 *    through environment variables if exec would normally strip it.
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
 *
 * 14. TODO: SYSV shared memory.  Processes potentially linked by shared memory
 *     segments should have their subgraph nodes interleaved.
 *
 *         shmget, shmat, shmdt, shmctl
 */

static void bad_open_flags(const char *path, int flags) __attribute__((noreturn));
static void bad_open_flags(const char *path, int flags)
{
    const char *s = NULL;
    if (flags & O_APPEND)   s = "O_APPEND is currently disallowed";
    if (flags & O_EXCL)     s = "O_EXCL is currently disallowed";
    if (flags & O_EVTONLY)  s = "O_EVTONLY is currently disallowed";
    if (!(flags & O_CREAT)) s = "O_CREAT is required for writing";
    if (!(flags & O_TRUNC)) s = "O_TRUNC is required for writing";
    die("open(\"%s\", 0x%x): %s", path, flags, s);
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
    int ignore = inside_libc;
    if (startswith(path, "/dev/"))
        ignore = 1;

    struct hash path_hash;
    int adjusted_flags = flags;
    if (!ignore) {
        // TODO: deal with O_NOFOLLOW and O_SYMLINK
        if (flags & (O_APPEND | O_EXCL | O_EVTONLY))
            bad_open_flags(path, flags);
        if (flags & O_RDWR)
            NOT_IMPLEMENTED("O_RDWR");
        remember_hash_path(&path_hash, path);
        if (flags & O_WRONLY) {
            if (~flags & (O_CREAT | O_TRUNC))
                bad_open_flags(path, flags);
            action_open_write(path, &path_hash);
            // Open read/write so that we can compute the file's hash upon
            // close.  TODO: Remove this if we compute hashes by intercepting
            // write() calls.
            adjusted_flags ^= O_RDWR | O_WRONLY;
        }
        else { // O_RDONLY
            if (!action_open_read(path, &path_hash)) {
                // File does not exist, no need to call open.
                errno = ENOENT;
                return -1;
            }
        }
    }

    int fd = real_open(path, adjusted_flags, mode);

    if (!ignore) {
        if (fd > 0) {
            fd_map_open(fd, flags, &path_hash);
        }
        else { // fd == -1
            if (flags & O_WRONLY)
                action_close_write(0);
        }
    }

    return fd;
}

int creat(const char *path, mode_t mode)
{
    return open(path, O_CREAT | O_TRUNC | O_WRONLY, mode);
}

// r+b is actually invalid, but in that case we fail only if fopen succeeds.
// This works around an annoying property of gcc (since fixed):
//     http://gcc.gnu.org/ml/gcc-patches/2009-09/msg01170.html
static const char valid_modes[][4] = {"r", "w", "rb", "wb", "r+b", ""};

FILE *fopen(const char *path, const char *mode)
{
    int i;
    for (i = 0; ; i++) {
        if (!strcmp(valid_modes[i], mode))
            break;
        if (!valid_modes[i][0])
            die("fopen(%s): unsupported mode '%s'", path, mode);
    }

    int flags = mode[0] == 'w' ? O_WRONLY : O_RDONLY;
    if (flags == O_WRONLY)
        mode = "w+"; // Open read/write for hashing purposes
    struct hash path_hash;
    remember_hash_path(&path_hash, path);
    if (flags & O_WRONLY)
        action_open_write(path, &path_hash);
    else { // O_RDONLY
        if (!action_open_read(path, &path_hash)) {
            // File does not exist, no need to call fopen.
            errno = ENOENT;
            return NULL;
        }
    }

    FILE *file = real_fopen(path, mode);

    if (!file) {
        if (errno != ENOENT)
            die("fopen(%s) failed: %s", path, strerror(errno));
    }
    else if (!strcmp(mode, "r+b"))
        die("fopen(%s): mode r+b is unsupported unless file doesn't exist");

    fd_map_open(fileno(file), flags | WO_FOPEN, &path_hash);
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
 * Both action_close_read and action_close_write are called before the actual
 * close call in order to take advantage of the open file descriptor.  For read,
 * we use fstat to verify that the file hasn't changed, and for write we reuse
 * the file descriptor to compute the hash of the written file.
 *
 * TODO: There is an unfortunate  race condition when writing a file.
 * We currently hash the output file after it is fully written, which means
 * that another process could modify the file before the close.  I believe
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
    struct fd_info *info = 0;
    if (!inside_libc) {
        info = fd_map_find(fd);
        if (info) {
            if (info->count == 1 && !(info->flags & WO_PIPE)) {
                if (info->flags & O_WRONLY)
                    action_close_write(fd);
            }
            fd_map_close(fd);
        }
    }

    int ret = real_close(fd);

    if (info && ret < 0)
        die("error on close(%d): %s", fd, strerror(errno));

    return ret;
}

int fclose(FILE *stream)
{
    int fd = fileno(stream);
    struct fd_info *info = fd_map_find(fileno(stream));
    if (info) {
        if (info->count == 1) {
            if (info->flags & O_WRONLY) {
                if (fflush(stream) < 0)
                    die("fflush failed: %s", strerror(errno));
                action_close_write(fd);
            }
        }
        fd_map_close(fd);
    }

    int ret = real_fclose(stream);

    if (info && ret < 0)
        die("error on fclose(%d): %s", fd, strerror(errno));

    return ret;
}

int fcloseall()
{
    NOT_IMPLEMENTED("fcloseall");
}

/*
 * Processes with pipes open are currently considered to share arbitrary
 * information in both directions (i.e., the data and the direction are
 * not tracked).  Therefore, pipe information is stored in the fd_map so
 * that subsequent calls to fork/exec can check for existant pipes.  If any
 * are found, the two process spines are joined (see action.c for details).
 */
int pipe(int fds[2])
{
    int ret = real_pipe(fds);

    if (!ret) {
        struct hash zero;
        memset(&zero, 0, sizeof(struct hash));
        fd_map_open(fds[0], O_RDONLY | WO_PIPE, &zero);
        fd_map_open(fds[1], O_WRONLY | WO_PIPE, &zero);
    }

    return ret;
}

int dup(int fd)
{
    // dup is easy to implement, but I don't understand how it can be
    // meaningfully used by build processes, so die for now.
    die("not implemented: dup(%d)", fd);
/*
    int fd2 = real_dup(fd);

    if (fd2 > 0) {
        struct fd_info *info = fd_map_find(fd);
        if (info)
            fd_map_open(fd2, info->flags, &info->path_hash);
    }

    return fd2;
*/
}

int dup2(int fd, int fd2)
{
    // Call the close stub to avoid duplicating action logic.  This produces
    // different behavior in the case where fd is not active.
    close(fd2);

    wlog("dup2(%d, %d)", fd, fd2);
    int ret = real_dup2(fd, fd2);

    if (ret > 0)
        fd_map_dup2(fd, fd2);

    return ret;
}

int fcntl(int fd, int cmd, long extra)
{
    wlog("fcntl(%d, %d, %ld)", fd, cmd, extra);

    // Track close-on-exec flag
    if (cmd == F_SETFD)
        fd_map_set_cloexec(fd, extra & 1);

    return real_fcntl(fd, cmd, extra);
}

// We do not need to intercept fstat since the necessary dependencies
// are tracked through open and close.

int lstat(const char *path, struct stat *buf) STAT_ALIAS(lstat);
int lstat(const char *path, struct stat *buf)
{
    die("not implemented: lstat(\"%s\", ...)", path);

    if (!inside_libc && !action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }
    // TODO: Coalesce this lstat with the one in action_read
    return real_lstat(path, buf);
}

int stat(const char *path, struct stat *buf) STAT_ALIAS(stat);
int stat(const char *path, struct stat *buf)
{
    // TODO: Don't pretend that lstat and stat are the same.  stat should be
    // modeled as the sequence of lstats that it is.
    if (!inside_libc && !action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }
    // TODO: Coalesce stat calls
    return real_stat(path, buf);
}

int access(const char *path, int amode)
{
    // TODO: make this the same as stat (i.e., not lstat)
    if (!inside_libc && !action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }
    return real_access(path, amode);
}

int chdir(const char *path)
{
    if (!action_lstat(path)) {
        errno = ENOENT;
        return -1;
    }

    int ret = real_chdir(path);

    if (ret < 0)
        die("chdirs(\"%s\") failed: %s", path, strerror(errno)); 

    return ret;
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
    // action_fork calls real_fork internally
    return action_fork();
}

pid_t vfork(void)
{
    // We put nontrivial logic after fork, and cannot be bothered
    // with the crippled semantics of vfork.
    return fork();
}

int execve(const char *path, const char *const argv[], char *const envp[])
{
    action_execve(path, argv, (const char* const*)envp);
    return real_execve(path, argv, (const char* const*)envp);
}

#define MAX_ARGS 32

// COLLECT_ARGV intentionally leaves va_end responsibility to the caller
#define COLLECT_ARGV() \
    const char *argv[MAX_ARGS+1]; \
    int i = 0; \
    argv[i++] = arg; \
    va_list ap; \
    va_start(ap, arg); \
    for(;; i++) { \
        if (i > MAX_ARGS) \
            die("exec* passed more than %d arguments", MAX_ARGS); \
        argv[i] = va_arg(ap, char*); \
        if (!argv[i]) \
            break; \
    }

// Declare environ for use in non -e versions of exec
#ifdef __APPLE__
// Mac shared libraries have to get environ via _NSGetEnviron()
extern char ***_NSGetEnviron(void);
#define GET_ENVIRON() (*_NSGetEnviron())
#else
extern char *const *environ;
#define GET_ENVIRON() environ
#endif

int execl(const char *path, const char *arg, ...)
{
    // Convert varargs into argv
    COLLECT_ARGV();
    va_end(ap);

    return execve(path, argv, GET_ENVIRON());
}

int execle(const char *path, const char *arg, ... /*, char *const envp[]*/)
{
    // Convert varargs into argv and envp
    COLLECT_ARGV();
    char *const *envp = va_arg(ap, char *const *);
    va_end(ap);

    return execve(path, argv, envp);
}

int execv(const char *path, const char *const argv[])
{
    return execve(path, argv, GET_ENVIRON());
}

int execvP(const char *file, const char *PATH, const char *const argv[])
{
    // Normally execvP works by repeatedly calling execve for each component
    // of the search path.  However, we'd prefer to call action_execve only
    // once, so we use a bunch of stat calls instead (at the cost of one extra
    // system call total).
    char buffer[PATH_MAX];
    file = search_path(buffer, file, PATH);
    if (!file)
        return -1;

    return execve(file, argv, GET_ENVIRON());
}

int execvp(const char *file, char *const argv[])
{
    // Note: Passing null for PATH is correct only because we're calling our
    // special version of execvP (which calls our version of search_path).
    return execvP(file, 0, (const char *const *)argv);
}

int execlp(const char *file, const char *arg, ...)
{
    // Convert varargs into argv
    COLLECT_ARGV();
    va_end(ap);

    return execvp(file, (char *const*)argv);
}

static const char signals[32][10] = {
    "?", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "?",
    "SIGFPE", "SIGKILL", "SIGBUS", "SIGSEGV", "SIGSYS", "SIGPIPE", "SIGALRM",
    "SIGTERM", "SIGURG", "SIGSTOP", "SIGTSTP", "SIGCONT", "SIGCHLD", "SIGTTIN",
    "SIGTTOU", "SIGID", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF",
    "SIGWINCH", "SIGINFO", "SIGUSR1", "SIGUSR2" };

pid_t waitpid(pid_t pid, int *status, int options)
{
    if (!status || (options & ~WNOHANG))
        die("unimplemented variant of waitpid: pid %d, status %d, options %d", pid, status != 0, options);

    // TODO: Ignore waits most or all of the time
    pid_t ret = real_waitpid(pid, status, options);

    if (ret < 0) {
        if ((options & WNOHANG) && errno == ECHILD)
            return ret;
        die("waitpid failed: %s", strerror(errno));
    }
    else if (status) {
        // !(options & WUNTRACED), so process either exited or caught a signal
        if (WIFSIGNALED(*status)) {
            int signal = WTERMSIG(*status);
            die("waitpid: child caught signal %s (%d)", signals[signal], signal);
        }
        else if (!WIFEXITED(*status))
            die("waitpid: confused?");
        else if (WEXITSTATUS(*status))
            die("waitpid: child exited with status %d", WEXITSTATUS(*status));
    }
    return ret;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
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
    action_exit(status);
    real__exit(status);
}

void _Exit(int status)
{
    _exit(status);
}

void exit(int status)
{
    action_exit(status);
    // TODO: calling exit assumes that atexit functions don't play tricks on us.
    // An alternative would be to call through to _exit instead.  Better yet,
    // we could intercept atexit and tmpfile and die or store the information.
    real_exit(status);
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

int fcntl_darwin(int fd, int cmd, long extra) DARWIN_ALIAS(fcntl);
int fcntl_darwin(int fd, int cmd, long extra)
{
    NOT_IMPLEMENTED("fcntl_darwin");
}

#endif

#pragma GCC visibility pop
