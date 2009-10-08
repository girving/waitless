// Per-process information

#include "process.h"
#include "real_call.h"
#include "env.h"
#include "util.h"
#include <errno.h>

// TODO: make this dynamic
#define MAX_PIDS 1024

struct process_map
{
    // Lists the pid of the process at each map index
    pid_t pids[MAX_PIDS];
    int killall; // if 1, no new entries can be added
    spinlock_t pids_lock;

    // Per-process info
    struct process processes[MAX_PIDS];
};

static spinlock_t map_lock;
static struct process_map *map;
static struct process *self_info;
static struct process *master_info;

void make_fresh_process_map()
{
    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");

    char process_path[PATH_MAX];
    strcpy(process_path, path_join(waitless_dir, "process.XXXXXXX")); // TODO: one unnecessary copy
    int fd = mkstemp(process_path);
    if (fd < 0)
        die("mkstemp failed: %s", strerror(errno));
    if (ftruncate(fd, sizeof(struct process_map)) < 0)
        die("ftruncate failed: %s", strerror(errno));
    if (real_close(fd) < 0)
        die("close failed: %s", strerror(errno));
    setenv(WAITLESS_PROCESS, process_path, 1);
}

static void initialize()
{
    if (map)
        return;

    spin_lock(&map_lock);

    const char *waitless_process = getenv(WAITLESS_PROCESS);
    if (!waitless_process)
        die("WAITLESS_PROCESS is not set");

    int fd = real_open(waitless_process, O_RDWR, 0);
    if (fd < 0)
        die("can't open process map %s: %s", waitless_process, strerror(errno));
    map = mmap(NULL, sizeof(struct process_map), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        die("can't mmap process map %s: %s", waitless_process, strerror(errno));
}

static void cleanup()
{
    killall();
    waitall();
}

struct process *new_process_info()
{
    initialize();
    pid_t pid = getpid();
    at_die = cleanup;

    spin_lock(&map->pids_lock);
    if (map->killall) {
        spin_unlock(&map->pids_lock);
        real_exit(1);
    }
    int i;
    for (i = 0; i < MAX_PIDS; i++) {
        if (map->pids[i] == pid) {
            spin_unlock(&map->pids_lock);
            die("new_process_info: entry already exists");
        }
        if (!map->pids[i])
            break;
    }
    if (i == MAX_PIDS) {
        spin_unlock(&map->pids_lock);
        die("too many processes");
    }
    map->pids[i] = pid;
    spin_unlock(&map->pids_lock);

    spin_lock(&map->processes[i].lock);
    self_info = map->processes + i;
    self_info->pid = pid;
    master_info = 0;
    return self_info;
}

struct process *find_process_info(pid_t pid)
{
    initialize();

    // No need to lock since pids is append only
    int i;
    for (i = 0; i < MAX_PIDS; i++)
        if (map->pids[i] == pid)
            return map->processes + i;
    die("process_info: no entry exists");
}

struct process *process_info()
{
    if (self_info)
        return self_info;
    self_info = find_process_info(getpid());
    return self_info;
}

struct process *lock_process()
{
    struct process *process = process_info();
    spin_lock(&process->lock);
    return process;
}

void unlock_process()
{
    spin_unlock(&self_info->lock);
}

struct process *lock_master_process()
{
    if (!master_info) {
        struct process *process = lock_process();
        if (!process->master)
            master_info = process;
        else
            master_info = find_process_info(process->master);
        unlock_process();
    }
    spin_lock(&master_info->lock);
    return master_info;
}

void unlock_master_process()
{
    spin_unlock(&master_info->lock);
}

void killall()
{
    initialize();

    // Set killall = 1 to prevent future entry creation
    spin_lock(&map->pids_lock);
    map->killall = 1;
    spin_unlock(&map->pids_lock);

    // Kill all existing processes
    int self = getpid(), i;
    for (i = 0; i < MAX_PIDS; i++) {
        int pid = map->pids[i];
        if (!pid)
            break;
        if (pid != self) // Don't kill ourself
            kill(pid, SIGKILL); 
    }
}
