// How to call the real version of a system call or libc function

#include "real_call.h"

/*
 * If PRELOAD=0, we're inside the waitless executable and can make system
 * calls normally.  If PRELOAD=1, we'll be loaded into subprocesses along
 * with stubs.o, and system calls must be made through dlsym.
 */

#if PRELOAD
    int inside_libc;

    #define SYSCALL_ALIAS(name, alias, ...) ({ \
        static int (*next)(); \
        if (!next) \
            next = (int (*)())dlsym(RTLD_NEXT, alias); \
        next(__VA_ARGS__); \
        })

    #define LIBCCALL_ALIAS(ret_t, name, alias, ...) ({ \
        static ret_t (*next)(); \
        if (!next) \
            next = (ret_t (*)())dlsym(RTLD_NEXT, alias); \
        inside_libc = 1; \
        ret_t ret = next(__VA_ARGS__); \
        inside_libc = 0; \
        ret; \
        })

    #define SYSCALL(name, ...) SYSCALL_ALIAS(name, #name, __VA_ARGS__)
    #define LIBCCALL(ret_t, name, ...) LIBCCALL_ALIAS(ret_t, name, #name, __VA_ARGS__)
#else
    #define LIBCCALL(ret_t, name, ...) ({ \
        extern ret_t name(); \
        name(__VA_ARGS__); \
        })

    #define LIBCCALL_ALIAS(ret_t, name, alias, ...) ({ \
        extern ret_t name() __asm("_" alias); \
        name(__VA_ARGS__); \
        })

    #define SYSCALL(name, ...) LIBCCALL(int, name, __VA_ARGS__)
    #define SYSCALL_ALIAS(name, alias, ...) LIBCCALL_ALIAS(int, name, alias, __VA_ARGS__)
#endif

int real_open(const char *path, int flags, mode_t mode)
{
    return SYSCALL(open, path, flags, mode);
}

int real_close(int fd)
{
    return SYSCALL(close, fd);
}

int real_pipe(int fds[2])
{
    return SYSCALL(pipe, fds);
}

int real_dup(int fd)
{
    return SYSCALL(dup, fd);
}

int real_dup2(int fd, int fd2)
{
    return SYSCALL(dup2, fd, fd2);
}

int real_fcntl(int fd, int cmd, long extra)
{
    return SYSCALL(fcntl, fd, cmd, extra);
}

int real_lstat(const char *path, struct stat *buf)
{
    return SYSCALL_ALIAS(lstat, STAT_NAME(lstat), path, buf);
}

int real_stat(const char *path, struct stat *buf)
{
    return SYSCALL_ALIAS(stat, STAT_NAME(stat), path, buf);
}

int real_fstat(int fd, struct stat *buf)
{
    return SYSCALL_ALIAS(fstat, STAT_NAME(fstat), fd, buf);
}

int real_access(const char *path, int amode)
{
    return SYSCALL(access, path, amode);
}

int real_chdir(const char *path)
{
    return SYSCALL(chdir, path);
}

pid_t real_fork(void)
{
    return SYSCALL(fork);
}

pid_t real_vfork(void)
{
    return SYSCALL(vfork);
}

int real_execve(const char *path, const char *const argv[], const char *const envp[])
{
    return SYSCALL(execve, path, argv, envp);
}

pid_t real_wait(int *status)
{
    return SYSCALL(wait, status);
}

pid_t real_wait3(int *status, int options, struct rusage *rusage)
{
    return SYSCALL(wait3, status, options, rusage);
}

pid_t real_wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
    return SYSCALL(wait4, status, options, rusage);
}

pid_t real_waitpid(pid_t pid, int *status, int options)
{
    return SYSCALL(waitpid, pid, status, options);
}

FILE *real_fopen(const char *path, const char *mode)
{
    return LIBCCALL(FILE*, fopen, path, mode);
}

int real_fclose(FILE *stream)
{
    return LIBCCALL(int, fclose, stream);
}

char *real_getcwd(char *buf, size_t n)
{
    return LIBCCALL(char*, getcwd, buf, n);
}

int real_mkstemp(char *template)
{
    return LIBCCALL(int, mkstemp, template);
}

void real__exit(int status)
{
    // Can't use SYSCALL since we need to declare __attribute__((noreturn))
#if PRELOAD
    typedef void (*next_t)(int) __attribute__((noreturn));
    static next_t next;
    if (!next)
        next = (next_t)dlsym(RTLD_NEXT, "_exit");
    next(status);
#else
    extern void _exit(int) __attribute__((noreturn));
    _exit(status);
#endif
}

void real_exit(int status)
{
    // Can't use LIBCCALL since we need to declare __attribute__((noreturn))
#if PRELOAD
    typedef void (*next_t)(int) __attribute__((noreturn));
    static next_t next;
    if (!next)
        next = (next_t)dlsym(RTLD_NEXT, "exit");
    inside_libc = 1;
    next(status);
#else
    extern void exit(int) __attribute__((noreturn));
    exit(status);
#endif
}
