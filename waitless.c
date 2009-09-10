// Waitless command launcher

#include "config.h"
#include "util.h"
#include "constants.h"
#include "shared_map.h"
#include "state.h"
#include "real_call.h"

#ifdef __APPLE__
#define PRELOAD_NAME "DYLD_INSERT_LIBRARIES"
#define PRELOAD_VALUE (PREFIX "/libwaitless.dylib")
#else
#define PRELOAD_NAME "LD_PRELOAD"
#define PRELOAD_VALUE (PREFIX "/libwaitless.so")
#endif

void usage()
{
    write_str(STDERR_FILENO,
        "usage: waitless cmd [args...]\n"
        "Run a command with automatic dependency analysis and caching.\n");
    real__exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argv[1][0] == '-')
        usage();

    // Add libwaitless.so to LD_PRELOAD (or the equivalent)
    if (getenv(PRELOAD_NAME))
        die("TODO: " PRELOAD_NAME " is already set, what are we supposed to do again?");
    setenv(PRELOAD_NAME, PRELOAD_VALUE, 1);

    // Set WAITLESS_DIR to $HOME/.waitless by default
    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir) {
        const char *home = getenv("HOME");
        if (!home)
            die("either WAITLESS_DIR or HOME must be set");
        setenv(WAITLESS_DIR, path_join(home, ".waitless"), 1);
        waitless_dir = getenv(WAITLESS_DIR);
    }

    // Make WAITLESS_DIR if it doesn't exist
    struct stat st;
    if (real_stat(waitless_dir, &st) < 0) {
        if (mkdir(waitless_dir, 0755) < 0)
            die("can't make WAITLESS_DIR '%s'", waitless_dir);
    }
    else if (!(st.st_mode & S_IFDIR))
        die("WAITLESS_DIR '%s' is not a directory (mode 0%6o)", waitless_dir, st.st_mode);

    // Make a fresh snapshot file and store its path in WAITLESS_SNAPSHOT
    char snapshot_path[PATH_MAX];
    strcpy(snapshot_path, path_join(waitless_dir, "snapshot.XXXXXXX")); // TODO: one unnecessary copy
    shared_map_init(&snapshot, mkstemp(snapshot_path));
    setenv(WAITLESS_SNAPSHOT, snapshot_path, 1);

    // Create and initialize the subgraph and stat cache if they don't exist
    shared_map_init(&subgraph, real_open(path_join(waitless_dir, subgraph.name), O_CREAT | O_WRONLY, 0755));
    shared_map_init(&stat_cache, real_open(path_join(waitless_dir, stat_cache.name), O_CREAT | O_WRONLY, 0755));

#ifdef __APPLE__
    // See http://koichitamura.blogspot.com/2008/11/hooking-library-calls-on-mac.html
    // DANGER: This presumably breaks any processes that depend on two-level lookup.
    setenv("DYLD_FORCE_FLAT_NAMESPACE", "1", 1);
#endif

    // Invoke the command
    pid_t pid = real_fork();
    if (pid < 0)
        die("fork failed");
    else if (!pid) {
        execvp(argv[1], argv+1);
        die("failed to exec %s", argv[1]);
    }

    // Wait
    int status = 1;
    real_wait(&status);

    // Remove snapshot
    unlink(snapshot_path);

    return status;
}
