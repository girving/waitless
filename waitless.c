// Waitless command launcher

#include "config.h"
#include "util.h"
#include "env.h"
#include "real_call.h"
#include "stat_cache.h"
#include "snapshot.h"
#include "subgraph.h"
#include "search_path.h"
#include "action.h"
#include "process.h"
#include <getopt.h>
#include <errno.h>

// Use an explicit forward declaration to avoid bringing in all of stdlib.h
extern int system(const char *command);

#ifdef __APPLE__
#define PRELOAD_NAME "DYLD_INSERT_LIBRARIES"
#define PRELOAD_VALUE (PREFIX "/libwaitless.dylib")
#else
#define PRELOAD_NAME "LD_PRELOAD"
#define PRELOAD_VALUE (PREFIX "/libwaitless.so")
#endif

static void usage()
{
    write_str(STDERR_FILENO,
        "usage: waitless [options] cmd [args...]\n"
        "       waitless [options]\n"
        "Run a command with automatic dependency analysis and caching.\n"
        "If cmd is omitted, options must include -c or -h.\n"
        "\n"
        "Options:\n"
        "   -c, --clean          forget all stored history\n"
        "   -v, --verbose        be extremely verbose\n"
        "   -d, --dump           dump all subgraph information\n"
        "   -h, --help           print this help message\n");
    real__exit(1);
}

static int dump;

static void cleanup(int signal)
{
    // Kill and wait for all subprocesses
    killall(); 
    waitall();

    if (dump)
        snapshot_dump();

    // Verify that the snapshot hasn't changed
    snapshot_verify();

    // Remove the snapshot and process map
    unlink(getenv(WAITLESS_SNAPSHOT));
    unlink(getenv(WAITLESS_PROCESS));

    if (signal)
        real__exit(1);
}

int main(int argc, char **argv)
{
    int clean = 0;
    int verbose = 0;

    const char *short_options = "+cvdh";
    struct option long_options[] = {
        {"clean",   no_argument, 0, 'c'},
        {"verbose", no_argument, 0, 'v'},
        {"dump",    no_argument, 0, 'd'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Parse all options
    for (;;) {
        int c = getopt_long(argc, argv, short_options, long_options, 0);
     
        // Break if we're done parsing options
        if (c == -1)
            break;
 
        switch (c) {
            case 'c': clean = 1; break;
            case 'v': verbose = 1; break;
            case 'd': dump = 1; break;
            case 'h': usage();
            default: return 1; // getopt_long already printed a message, so exit
        }
    }

    if (argc == optind && !clean && !dump)
        usage();
    const char **cmd = argc == optind ? 0 : (const char**)(argv+optind);

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

    // To clean, remove subgraph, stat_cache, and inverse.
    if (clean) {
        char clean[1024];
        snprintf(clean, sizeof(clean), "cd %s && /bin/rm -rf subgraph stat_cache inverse spine.*", waitless_dir);
        int r = system(clean);
        if (r)
            die("full clean (-C) failed, status %d", r);
    }

    // Create and initialize the subgraph and stat cache if they don't exist
    subgraph_init();
    stat_cache_init();

    if (dump)
        subgraph_dump();

    // Continue only if we have a command to run
    if (!cmd)
        return 0;

    // Make a fresh snapshot data structure for this run and store its path in
    // WAITLESS_SNAPSHOT.  The snapshot we keep track of what we consider to be
    // the true contents of files for this run.  Note that this does not mean
    // that these files will have consistent contents after waitless completes
    // or indeed at any point in time whatsoever, only that all processes under
    // waitless will appear to have seen a consistent set of files.
    make_fresh_snapshot();

    // Make a fresh process map
    make_fresh_process_map();

    // Always perform cleanup steps
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    // Set verbose flag if desired
    if (verbose)
        setenv(WAITLESS_VERBOSE, "1", 1);

    // Add libwaitless.so to LD_PRELOAD (or the equivalent)
    if (getenv(PRELOAD_NAME))
        die("TODO: " PRELOAD_NAME " is already set, what are we supposed to do again?");
    setenv(PRELOAD_NAME, PRELOAD_VALUE, 1);

#ifdef __APPLE__
    // See http://koichitamura.blogspot.com/2008/11/hooking-library-calls-on-mac.html
    // DANGER: This presumably breaks any processes that depend on two-level lookup.
    setenv("DYLD_FORCE_FLAT_NAMESPACE", "1", 1);
#endif

    // Replace stdin with /dev/null (waitless processes should not be interactive)
    int null = real_open("/dev/null", O_RDONLY, 0);
    if (real_dup2(null, STDIN_FILENO) < 0)
        die("failed to open /dev/null");
    real_close(null);

    // Find the correct absolute path to exec
    char buffer[PATH_MAX];
    const char *path = search_path(buffer, cmd[0], 0);
    if (!path)
        die("%s: command not found", cmd[0]);

    // Invoke the command
    pid_t pid = real_fork();
    if (pid < 0)
        die("fork failed");
    else if (!pid) {
        // Create process info
        new_process_info(); 
        unlock_process();

        // Create the root exec node and then exec
        extern const char **environ;
        action_execve(path, cmd, environ);
        die("failed to exec %s: ", cmd[0], strerror(errno));
    }

    // Wait for all children.
    int status = waitall();

    cleanup(0);
    return status;
}
