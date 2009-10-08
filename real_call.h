// How to call the real version of a system call or libc function

#ifndef __real_call_h__
#define __real_call_h__

/*
 * Most of the waitless code can be used in two possible ways:
 *
 * 1. Linked into libwaitless.so and loaded into processes via LD_PRELOAD.
 * 2. Linked into interface executables like waitless normally.
 *
 * If some of this code wants to call a system call like open(), we need to make
 * sure we call the real version instead of our fancy interception stub.  The
 * necessary logic to call the real version is wrapped up in a stub function
 * called 'real_open'.
 *
 * In order to make it easier to check that all necessary calls go through this
 * interface, we minimize the number of places we include header files which
 * declare dangerous functions.  In particular, no code in waitless should ever
 * include stdio; the few functions that are safe to use are declared manually.
 *
 * Along with the various real_ signatures, this header declares various
 * structures and constants so that other files needn't get them from dangerous
 * header files like fcntl.h.
 */

#include "arch.h"

struct stat;
struct rusage;
typedef struct FILE FILE;

// See fcntl.h or man open
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_NONBLOCK 0x0004
#define O_APPEND   0x0008
#define O_SYNC     0x0080
#define O_SHLOCK   0x0010
#define O_EXLOCK   0x0020
#define O_NOFOLLOW 0x0100
#define O_CREAT    0x0200
#define O_TRUNC    0x0400
#define O_EXCL     0x0800
#define O_EVTONLY  0x8000

// See fcntl.h or man fcntl
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

// See sys/mman.h or man mmap
#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04
#define MAP_SHARED 0x0001
#define MAP_FAILED ((void *)-1)

// See unistd.h or man stdout
#define  STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// See unistd.h or man lseek
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// See dlfcn.h or man dlsym
#define RTLD_NEXT ((void*)-1)

// Declare system call wrappers
extern int real_open(const char *path, int flags, mode_t mode);
extern int real_close(int fd);
extern int real_pipe(int fds[2]);
extern int real_dup(int fd);
extern int real_dup2(int fd, int fd2);
extern int real_fcntl(int fd, int cmd, long extra);
extern int real_lstat(const char *path, struct stat *buf);
extern int real_stat(const char *path, struct stat *buf);
extern int real_fstat(int fd, struct stat *buf);
extern int real_access(const char *path, int amode);
extern int real_chdir(const char *path);
extern pid_t real_fork(void);
extern pid_t real_vfork(void);
extern int real_execve(const char *path, const char *const argv[], const char *const envp[]);
extern pid_t real_wait(int *status);
extern pid_t real_wait3(int *status, int options, struct rusage *rusage);
extern pid_t real_wait4(pid_t pid, int *status, int options, struct rusage *rusage);
extern pid_t real_waitpid(pid_t pid, int *status, int options);
extern void real__exit(int status) __attribute__((noreturn));

// Are we inside an intercepted libc function?  Used to avoid re-executing
// wrapper logic if we manage to intercept both a system call and it's libc
// equivalent.  TODO: thread safety
extern int inside_libc;

// Declare libc wrappers.  Each of these sets inside_libc = 1 for the duration
// of the call; if we managed to intercept the underlying system call as well
// this lets us avoid executing waitless logic twice.
extern FILE *real_fopen(const char *path, const char *mode);
extern int real_fclose(FILE *stream);
extern void real_exit(int status) __attribute__((noreturn));
extern char *real_getcwd(char *buf, size_t n);
extern int real_mkstemp(char *template);

// These functions are not intercepted, so we declare them directly.  As they
// become intercepted in future, their names will change to start with real_.
extern int fileno(FILE *stream);
extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern off_t lseek(int fd, off_t offset, int whence);
extern void *dlsym(void* handle, const char* symbol);
extern char *getenv(const char *name);
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);
extern void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
extern int mkdir(const char *path, mode_t mode);
extern int mkstemp(char *template);
extern int unlink(const char *path);
extern int ftruncate(int fd, off_t length);
extern int getpid(void);
extern int kill(pid_t pid, int signal);
extern int fflush(FILE *stream);

#endif
