// How to call the real version of a system call or libc function

#include "real_call.h"

/*
 * If PRELOAD=0, we're inside the waitless executable and can make system
 * calls normally.  If PRELOAD=1, we'll be loaded into subprocesses along
 * with stubs.o, and system calls must be made through dlsym.
 */

#if PRELOAD
    int inside_libc;

    #define SYSCALL(name, ...) ({ \
        static int (*next)(); \
        if (!next) \
            next = (int (*)())dlsym(RTLD_NEXT, #name); \
        next(__VA_ARGS__); \
        })

    #define LIBCCALL(ret_t, name, ...) ({ \
        static ret_t (*next)(); \
        if (!next) \
            next = (ret_t (*)())dlsym(RTLD_NEXT, #name); \
        inside_libc = 1; \
        ret_t ret = next(__VA_ARGS__); \
        inside_libc = 0; \
        ret; \
        })
#else
    #define LIBCCALL(ret_t, name, ...) ({ \
        extern ret_t name(); \
        name(__VA_ARGS__); \
        })

    #define SYSCALL(name, ...) LIBCCALL(int, name, __VA_ARGS__)
#endif

int real_open(const char *path, int flags, mode_t mode)
{
    return SYSCALL(open, path, flags, mode);
}

int real_close(int fd)
{
    return SYSCALL(close, fd);
}

int real_lstat(const char *path, struct stat *buf)
{
    return SYSCALL(lstat, path, buf);
}

int real_stat(const char *path, struct stat *buf)
{
    return SYSCALL(stat, path, buf);
}

int real_fstat(int fd, struct stat *buf)
{
    return SYSCALL(fstat, fd, buf);
}

int real_access(const char *path, int amode)
{
    return SYSCALL(access, path, amode);
}

pid_t real_fork(void)
{
    return SYSCALL(fork);
}

pid_t real_vfork(void)
{
    return SYSCALL(vfork);
}

int real_execve(const char *path, char *const argv[], char *const envp[])
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

void real__exit(int status)
{
    // Can't use SYSCALL since we need to declare __attribute__((noreturn))
#ifdef PRELOAD
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

FILE *real_fopen(const char *path, const char *mode)
{
    return LIBCCALL(FILE*, fopen, path, mode);
}

int real_fclose(FILE *stream)
{
    return LIBCCALL(int, fclose, stream);
}
