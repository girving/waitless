// Shared state for waitless

#include "state.h"
#include "constants.h"
#include "util.h"
#include "real_call.h"

struct subgraph_entry
{
    // TODO
};

struct snapshot_entry
{
    // Information about the past and present of this file during the
    // current invocation.
    unsigned was_read : 1;
    unsigned was_written : 1;
    unsigned currently_reading : 1;
    unsigned currently_writing : 1;

    // The hash that we consider current
    struct hash hash;
};

struct stat_cache_entry
{
    // Last known stat information
    ino_t inode;              // inode number (stat.st_ino)
    struct timespec modified; // last modification time
    off_t size;               // file size

    // Last known hash of the file's contents
    struct hash hash;
};

// TODO: Rethink default counts and make them resizable
struct shared_map subgraph = { "subgraph", sizeof(struct subgraph_entry), 1<<10 };
struct shared_map snapshot = { "snapshot.XXXXXXX", sizeof(struct snapshot_entry), 1<<10 };
struct shared_map stat_cache = { "stat_cache", sizeof(struct stat_cache_entry), 1<<10 };

void initialize()
{
    static int initialized = 0;
    if (initialized)
        return;
    initialized = 1;

    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");
    shared_map_open(&subgraph, path_join(waitless_dir, subgraph.name));
    shared_map_open(&stat_cache, path_join(waitless_dir, stat_cache.name));

    const char *waitless_snapshot = getenv(WAITLESS_SNAPSHOT);
    if (!waitless_snapshot)
        die("WAITLESS_SNAPSHOT not set");
    shared_map_open(&snapshot, waitless_snapshot);
}
